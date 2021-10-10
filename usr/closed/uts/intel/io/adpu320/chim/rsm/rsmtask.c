/*$Header: /vobs/u320chim/src/chim/rsm/rsmtask.c   /main/38   Fri Aug 22 16:19:48 2003   hu11135 $*/

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
*  Module Name:   RSMTASK.C
*
*  Description:
*                 Codes to implement resource management layer
*
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         NONE
*
*  Entry Point(s):
*                 SCSIRSetUnitHandle
*                 SCSIRFreeUnitHandle
*                 SCSIRQueueHIOB
*                 SCSIRQueueSpecialHIOB
*                 SCSIRFrontEndISR
*                 SCSIRBackEndISR
*
***************************************************************************/

#include "scsi.h"
#include "rsm.h"

/*********************************************************************
*
*  SCSIRSetUnitHandle
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
void SCSIRSetUnitHandle (SCSI_HHCB SCSI_HPTR hhcb,
                         SCSI_UNIT_CONTROL SCSI_UPTR targetUnit,
                         SCSI_UEXACT8 scsiID,
                         SCSI_UEXACT8 luNumber)
{
   /* inform hardware management */
   SCSIHSetUnitHandle(hhcb,targetUnit,scsiID,luNumber);

   /* setup pointer to target control */
   targetUnit->targetControl = &hhcb->SCSI_HP_targetControl[scsiID];
}

/*********************************************************************
*
*  SCSIRFreeUnitHandle
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
void SCSIRFreeUnitHandle (SCSI_UNIT_HANDLE unitHandle)
{
   /* inform hardware management */
   SCSIHFreeUnitHandle(unitHandle);
}

/*********************************************************************
*
*  SCSIRQueueHIOB 
*
*     Queue iob to resource management queues
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:
*                  
*********************************************************************/
#if !SCSI_STREAMLINE_QIOPATH
void SCSIRQueueHIOB (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_HHCB SCSI_HPTR hhcb = SCSI_TARGET_UNIT(hiob)->hhcb;

   /* fresh the hiob status and working area */
   SCSI_rFRESH_HIOB(hiob);

   /* start with checking criteria (max tags/nontags) */
   SCSIrCheckCriteria(hhcb,hiob);
}
#endif /* !SCSI_STREAMLINE_QIOPATH */

/*********************************************************************
*
*  SCSIRQueueSpecialHIOB
*
*     Queue special iob to special resource management queue
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:       This routine will process all special request
*                 through HIOB.
*                  
*********************************************************************/
void SCSIRQueueSpecialHIOB (SCSI_HIOB SCSI_IPTR hiob)
{
#if SCSI_TARGET_OPERATION
   SCSI_HHCB SCSI_HPTR hhcb;
   SCSI_NEXUS SCSI_XPTR nexus;
#endif /* SCSI_TARGET_OPERATION */

#if SCSI_TARGET_OPERATION
   OSDDebugPrint(8,("\nEntered SCSIRQueueSpecial "));
   OSDDebugPrint(8,("\nhiob->cmd = %xh ",hiob->cmd));
#endif /* SCSI_TARGET_OPERATION */

   /* fresh the hiob status and working area */
   SCSI_rFRESH_HIOB(hiob);

   switch(hiob->cmd)
   {
      case SCSI_CMD_ABORT_TASK:
         SCSIrAbortTask(hiob);
         break;

      case SCSI_CMD_ABORT_TASK_SET:
         SCSIrAbortTaskSet(hiob);
         break;

      case SCSI_CMD_RESET_BUS:
         SCSIrScsiBusReset(hiob);
         break;

      case SCSI_CMD_RESET_TARGET:
      case SCSI_CMD_LOGICAL_UNIT_RESET:
         SCSIrTargetReset(hiob);
         break;

      case SCSI_CMD_RESET_HARDWARE:
         SCSIrResetHardware(hiob);
         break;

      case SCSI_CMD_PROTO_AUTO_CFG:
#if SCSI_TARGET_OPERATION
         hhcb = SCSI_INITIATOR_UNIT(hiob);
         if (hhcb->SCSI_HF_targetMode)
         {
            /* Pass request to hardware management layer */
            SCSIHQueueSpecialHIOB(hiob);
         }
         else
         {
            /* RSM completion */  
            hiob->stat = SCSI_SCB_COMP;
            SCSIrCompleteSpecialHIOB(hiob);
         }
#else
         /* RSM completion */  
         hiob->stat = SCSI_SCB_COMP;
         SCSIrCompleteSpecialHIOB(hiob);
#endif /* SCSI_TARGET_OPERATION */ 
         break;

      case SCSI_CMD_UNFREEZE_QUEUE:
         /* Unfreeze the device queue and dispatch any queued HIOBs */
         /* if the queue is not frozen. */
         SCSIrUnFreezeDeviceQueue(SCSI_TARGET_UNIT(hiob)->targetControl,
                                  SCSI_FREEZE_ERRORBUSY);
         SCSIrDispatchQueuedHIOB(hiob);

         /* Set success status and post back. */
         hiob->stat = SCSI_SCB_COMP;
         SCSIrCompleteSpecialHIOB(hiob);
         break;

#if SCSI_TARGET_OPERATION
      case SCSI_CMD_REESTABLISH_AND_COMPLETE:
      case SCSI_CMD_REESTABLISH_INTERMEDIATE:
         SCSI_rTARGETMODE_HIOB(hiob);
         nexus = SCSI_NEXUS_UNIT(hiob);
         hhcb = nexus->hhcb;
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
         if (nexus->SCSI_XP_iuReqChange)
         {
            /* This request is invalid if iuReqChange
             * has occurred.
             */
            hiob->haStat = SCSI_HOST_ABT_IU_REQ_CHANGE;
            hiob->stat = SCSI_SCB_ABORTED;
            OSD_COMPLETE_SPECIAL_HIOB(hiob);
         }
         else
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */         
         {
            /* indicate we have an active IOB for this nexus */
            /* currently we only allow one hiob per nexus */
            /* if we support more then change this to an 8 bit field */ 
            nexus->SCSI_XP_hiobActive = 1;
                  
            if (!(nexus->SCSI_XF_busHeld))
            {
               /* SCB needs to be allocated */
           
               if (!SCSI_IS_NULL_SCB(hiob->scbNumber = SCSI_rGETSCB(hhcb,hiob)))
               { 
                  /* scb available, queue to hardware management layer */
                  SCSIHQueueSpecialHIOB(hiob);
               }
               else
               {         
                  /* queue it to host queue */
                  SCSIrHostQueueAppend(hhcb,hiob);
               }      
            }
            else
            {  
               /* SCSI bus was held - just give to HWM */ 
               /* the scb was not freed because the bus was held */ 
               hiob->scbNumber = hhcb->SCSI_HP_busHeldScbNumber;

               /* Null the busHeldScbNumber */
               SCSI_SET_NULL_SCB(hhcb->SCSI_HP_busHeldScbNumber);

               /* indicator for where to return scb */
               if (hhcb->SCSI_HP_estScb)
               {
                  hiob->SCSI_IP_estScb = 1;
               }

               SCSIHQueueSpecialHIOB(hiob);
            }
         }
         break;

      case SCSI_CMD_ESTABLISH_CONNECTION:
         /* Add this hiob (iob) to the available pool */
         SCSI_rTARGETMODE_HIOB(hiob);
         hhcb = SCSI_INITIATOR_UNIT(hiob);
         hiob->queueNext = SCSI_NULL_HIOB;
         if (hhcb->SCSI_HF_targetHoldEstHiobs)
         {
            /* Cannot queue to HWM layer */
            SCSIrQueueEstHiobPool(hhcb,hiob); 
         }
         else
         {
            SCSIrQueueEstHiobToHwm(hhcb,hiob);
         }
         break;

      case SCSI_CMD_ABORT_NEXUS:
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
         if (SCSI_NEXUS_UNIT(hiob)->SCSI_XP_iuReqChange)
         {
            /* This request is invalid if iuReqChange
             * has occurred.
             */
            hiob->haStat = SCSI_HOST_ABT_IU_REQ_CHANGE;
            hiob->stat = SCSI_SCB_ABORTED;
            OSD_COMPLETE_SPECIAL_HIOB(hiob);
         }
         else
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */         
         {
            /* Abort any IOBs associated with this nexus and release the SCSI  */
            /* bus - if held.                                                  */
            /* Send request down to HWM layer.                                 */
            SCSIHQueueSpecialHIOB(hiob);

            /* Note that; the call to find HIOBs in the Host Q for this */
            /* nexus is made on the completion of HWM request (see      */
            /* SCSIrCompleteSpecialHIOB).                               */
         }
         break;

#if SCSI_MULTIPLEID_SUPPORT
      case SCSI_CMD_ENABLE_ID:
      case SCSI_CMD_DISABLE_ID:
         /* Send request down to HWM layer. */
         SCSIHQueueSpecialHIOB(hiob); 
         break; 
#endif /* SCSI_MULTIPLEID_SUPPORT */

#endif /* SCSI_TARGET_OPERATION */

      default:
         break;
   }
}

/*********************************************************************
*
*  SCSIRBackEndISR
*
*     Back end processing of ISR
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine will actually handle interrupt event
*                 though it was cleared by SCSIHFrontEndISR.
*
*********************************************************************/
#if !SCSI_STREAMLINE_QIOPATH
int SCSIRBackEndISR (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSIHBackEndISR(hhcb);

   SCSIrPostDoneQueue(hhcb);

   return(0);
}
#endif /* !SCSI_STREAMLINE_QIOPATH */

/*********************************************************************
*
*  SCSIRCheckHostIdle
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
SCSI_UINT8 SCSIRCheckHostIdle (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_UINT i;

   /* check if there is any outstanding request in criteria queue */
   if (hhcb->SCSI_WAIT_HEAD != SCSI_NULL_HIOB)
   {
      return(SCSI_NOT_IDLE);
   }

#if SCSI_TARGET_OPERATION
   if (hhcb->SCSI_HF_initiatorMode)
   {
#endif /* SCSI_TARGET_OPERATION */ 
      /* check if there is any outstanding request in resource queue */
      for (i = 0; i < SCSI_MAXDEV; i++)
      {
         if (hhcb->SCSI_HP_targetControl[i].queueHead != SCSI_NULL_HIOB)
         {
            return(SCSI_NOT_IDLE);
         }
      }
#if SCSI_TARGET_OPERATION
   }
#endif /* SCSI_TARGET_OPERATION */ 

   /* check if there is any outstanding request in hardware */
   /* management layer */
   return(SCSIHCheckHostIdle(hhcb));
}

/*********************************************************************
*
*  SCSIRCheckDeviceIdle
*
*     Get the check device is idle
*
*  Return Value:  SCSI_IDLE     - device is idle
*                 SCSI_NOT_IDLE - device is not idle
*                  
*  Parameters:    hhcb
*                 target SCSI ID
*
*  Remarks:       This routine will check for any outstanding
*                 request of particular device ID in the host
*                 queue and device queue.  This routine should be
*                 called by the task that's running outside the
*                 command complete process such as
*                 SCSIAdjustTargetProfile routine.
*
*********************************************************************/
SCSI_UINT8 SCSIRCheckDeviceIdle (SCSI_HHCB SCSI_HPTR hhcb,
                                 SCSI_UEXACT8 targetID)
{
   SCSI_HIOB SCSI_IPTR hiob;

   hiob = hhcb->SCSI_WAIT_HEAD;

   /* Check if there is any outstanding request for this target */
   /* in the host queue */
   while (hiob != SCSI_NULL_HIOB)
   {
      if (SCSI_TARGET_UNIT(hiob)->scsiID == targetID)
      {
         return(SCSI_NOT_IDLE);
      }

      /* Next HIOB */
      hiob = hiob->queueNext;
   }

   /* Check if there is any outstanding request for this target */
   /* in the device queue */
   if (hhcb->SCSI_HP_targetControl[targetID].queueHead != SCSI_NULL_HIOB)
   {
      return(SCSI_NOT_IDLE);
   }

   /* Check the hardware management layer */
   return(SCSIHCheckDeviceIdle(hhcb, targetID));
}

/*********************************************************************
*
*  SCSIrCompleteHIOB
*
*     Process HIOB posted from hardware management layer
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:
*
*********************************************************************/
void SCSIrCompleteHIOB (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_HHCB SCSI_HPTR hhcb = SCSI_TARGET_UNIT(hiob)->hhcb;
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl;
   
   targetControl = SCSI_TARGET_UNIT(hiob)->targetControl;

   if (hiob->stat != SCSI_SCB_FROZEN)
   {
      if (hiob->trgStatus == SCSI_UNIT_QUEFULL)
      {
         /* The queue has been already frozen by the hardware layer; just  */
         /* set the SCSI_FREEZE_QFUL bit in the freezemap variable         */
         SCSIrFreezeDeviceQueue(targetControl, SCSI_FREEZE_QFULL);
      }
   }

   /* free scb associated */
   SCSIrFreeScb(hhcb,hiob);

   if (hiob->stat == SCSI_SCB_FROZEN)
   {
      /* Record the fact that the target has one more command active. */
      targetControl->activeScbs++;

      /* Queue the SCB back in the Device Queue */
      SCSIrDeviceQueueAppend(targetControl,hiob);
   }

   else
   {
      /* Put HIOB into Done queue - Add element at tail */
      hiob->queueNext = SCSI_NULL_HIOB;      
      if (hhcb->SCSI_DONE_TAIL==SCSI_NULL_HIOB)
      {
         /* This is the 1st element. Init SCSI_DONE_HEAD accordingly */
         hhcb->SCSI_DONE_HEAD = hiob;
      }
      else
      {
         /* DoneQ already have at least 1 element */
         hhcb->SCSI_DONE_TAIL->queueNext=hiob;
      }
      hhcb->SCSI_DONE_TAIL = hiob;
   }
}

/*********************************************************************
*
*  SCSIrCompleteSpecialHIOB
*
*     Process special HIOB posted from hardware management layer
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:
*
*********************************************************************/
void SCSIrCompleteSpecialHIOB (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_HHCB SCSI_HPTR hhcb;
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl;
#if SCSI_TARGET_OPERATION
   SCSI_UEXACT8 haStatus;
   SCSI_NEXUS SCSI_XPTR nexus;
   SCSI_HIOB SCSI_IPTR newHiob;
#endif /* SCSI_TARGET_OPERATION */

#if SCSI_TARGET_OPERATION
   OSDDebugPrint(8,("\n Entered SCSIrCompleteSpecialHIOB "));
   OSDDebugPrint(8,("\n hiob->cmd = %xh ",hiob->cmd));
#endif /* SCSI_TARGET_OPERATION */

   switch (hiob->cmd)
   {
      case SCSI_CMD_ABORT_TASK:
         hhcb = SCSI_TARGET_UNIT(SCSI_RELATED_HIOB(hiob))->hhcb;
         SCSIrPostDoneQueue(hhcb);           /* Post back all HIOB(s) */
         break;

      case SCSI_CMD_ABORT_TASK_SET:
         hhcb = SCSI_TARGET_UNIT(hiob)->hhcb;
         targetControl = SCSI_TARGET_UNIT(hiob)->targetControl;

         /* Try to unfreeze the queue by clearing the ABTTSKSET freeze-bit */
         /* only if this is the last Abort_Task_Set returned from the HWM layer. */
         if (!(--targetControl->activeABTTSKSETs))
         {
            SCSIrUnFreezeDeviceQueue(targetControl, SCSI_FREEZE_ABTTSKSET);
            SCSIrDispatchQueuedHIOB(hiob);
         }

         SCSIrPostDoneQueue(hhcb);           /* Post back all HIOB(s) */
         break;

      case SCSI_CMD_RESET_BUS:
         hhcb = SCSI_INITIATOR_UNIT(hiob);
         /* Unfreeze RSM Layer */
         SCSI_rUNFREEZEALLDEVICEQUEUES(hhcb,SCSI_FREEZE_MAPS);
         /* Check if target mode bus held SCB to be freed. */ 
         SCSI_rFREEBUSHELDSCB(hhcb);
         /* Queue Establish Connection HIOBs which 
          * may have been queued to HIOB queue during reset.
          */
         SCSI_rQUEUEESTHIOBS(hhcb);
         SCSIrPostDoneQueue(hhcb);           /* Post back all HIOB(s) */

         break;

      case SCSI_CMD_RESET_TARGET:
      case SCSI_CMD_LOGICAL_UNIT_RESET:
         hhcb = SCSI_TARGET_UNIT(hiob)->hhcb;
         targetControl = SCSI_TARGET_UNIT(hiob)->targetControl;

         /* Free scb associated.  This call will clear the queue full     */
         /* condition as well if any.  But, the queue is still be frozen  */
         /* due to the TR freeze-bit has not been cleared.  Therefore,    */
         /* the call to unfreeze the queue below will dispatch any queued */
         /* HIOB(s).  Normally, TR is the last one return from HWM layer. */
         SCSIrFreeScb(hhcb,hiob);

         /* Try to unfreeze the queue by clearing the TR freeze-bit  */
         /* only if this is the last TR returned from the HWM layer. */
         if (!(--targetControl->activeTRs))
         {
            SCSIrUnFreezeDeviceQueue(targetControl, SCSI_FREEZE_TR);
            SCSIrDispatchQueuedHIOB(hiob);
         }

         SCSIrPostDoneQueue(hhcb);           /* Post back all HIOB(s) */
         break;

#if SCSI_TARGET_OPERATION
      case SCSI_CMD_ESTABLISH_CONNECTION:
         
         /* All establish connection completions are posted to OSM     */
         /* immediately (Note; DONE queue size is equal to numberScbs, */
         /* and an SCB is not allocated for an establish connection    */
         /* IOB, so queueing to DONE queue could result in overflow.)  */ 
         hhcb = SCSI_NEXUS_UNIT(hiob)->hhcb;

         /* Was there an exception */     
         if (hiob->stat != SCSI_SCB_COMP &&
             hiob->stat != SCSI_TASK_CMD_COMP && 
             hiob->haStat != SCSI_HOST_TRUNC_CMD &&
             hiob->haStat != SCSI_HOST_ABT_HOST)
         {
            
            /* return est scb to available pool */
            SCSI_rRETURNESTSCB(hhcb,hiob);

#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
            if ((hiob->stat == SCSI_SCB_ABORTED) &&
                (hiob->haStat == SCSI_HOST_IU_STATE_CHANGE))
            {
               /* Need to mark all active nexus for this initiator
                * as iuReqChange and abort any I/Os queued in RSM
                * layer.
                */
               SCSIrTargetFindAndSetNexusIuReqChange(hhcb,SCSI_NEXUS_UNIT(hiob));
               SCSIrFindAndAbortTargetHIOBs(hhcb,SCSI_NEXUS_UNIT(hiob),hiob->haStat);
            }
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */

            /* If an Establish Connection HIOB completes with an exception
             * other than stat = SCSI_TASK_CMD_COMP or haStat SCSI_HOST_TRUNC_CMD
             * then requeue an don't post to OSM.
             */
            if (hhcb->SCSI_HF_targetHoldEstHiobs)
            {
               SCSIrFreeNexus(hhcb,SCSI_NEXUS_UNIT(hiob));
               SCSI_rFRESH_HIOB(hiob);
               SCSI_rTARGETMODE_HIOB(hiob);
               SCSIrQueueEstHiobPool(hhcb,hiob);
            }
            else
            {
               SCSI_rFRESH_HIOB(hiob);
               SCSI_rTARGETMODE_HIOB(hiob);
               /* We're reusing the nexus - so clean it up. */
               SCSI_rFRESH_NEXUS(SCSI_NEXUS_UNIT(hiob));
               /* Indicate that hiob is queued to HWM layer */
               hiob->SCSI_IP_estQueuedToHwm = 1;
               hiob->queueNext = SCSI_NULL_HIOB;
               /* assume est scb available as we just queued one */
               hiob->scbNumber = SCSI_rGETESTSCB(hhcb,hiob);
               SCSIHQueueSpecialHIOB(hiob); 
            }
         }
         else
         {
            if (SCSI_NEXUS_UNIT(hiob)->SCSI_XF_busHeld)
            {    
               OSDDebugPrint(8,("\nBus Held - SCB not freed "));
               /* Don't free establish connection scb - reuse for response. */ 
               /* save scbNumber and est scb indicator in hhcb */
               hhcb->SCSI_HP_busHeldScbNumber = hiob->scbNumber;
               hhcb->SCSI_HP_estScb = 1;
            }
            else
            {
               /* return est scb to available pool */
               SCSI_rRETURNESTSCB(hhcb,hiob);

               if (!hhcb->SCSI_HF_targetHoldEstHiobs)
               {
                  /* An Est SCB is returned so see if we have an
                   * Est HIOB to queue to HWM layer.
                   */
                  /* no need to call SCSI_rFRESH_HIOB etc - done before
                   * put on queue.
                   */
                  if ((newHiob = SCSIrHiobPoolRemove(hhcb)) != SCSI_NULL_HIOB)
                  { 
                     SCSIrQueueEstHiobToHwm(hhcb,newHiob);
                  }
               }
            }

            if (hiob->stat == SCSI_TASK_CMD_COMP)
            {
               /* Task management command received */
               /* Pass the IOB to the OSM then perform request */  
               haStatus = hiob->haStat;
               nexus = SCSI_NEXUS_UNIT(hiob);
               OSD_COMPLETE_SPECIAL_HIOB(hiob);
               SCSIrTargetTaskManagementRequest(hhcb,haStatus,nexus);
            } 
            else
            {
               if (hiob->haStat == SCSI_HOST_ABT_HOST)
               {
                  /* This HIOB was aborted by the host. Assumes that the OSM
                   * quiesced the target mode side prior to aborting. We must
                   * free the nexus.
                   */
                  SCSIrFreeNexus(hhcb,SCSI_NEXUS_UNIT(hiob));
               }
               /* Maintain IOB completion order - very important for SCSI for 
                  normal IOB processing as OSM needs to be able to determine if
                  overlapped command etc.
                */ 
               SCSIrPostDoneQueue(hhcb);    /* Post back all HIOB(s) */
               OSD_COMPLETE_SPECIAL_HIOB(hiob);
            }
         }
         return;      
         
      case SCSI_CMD_REESTABLISH_AND_COMPLETE:
      case SCSI_CMD_REESTABLISH_INTERMEDIATE:
         /* Target mode Iob completion */
         /* free scb associated */
         nexus = SCSI_NEXUS_UNIT(hiob); 
         hhcb = nexus->hhcb;
 
         if (nexus->SCSI_XF_busHeld)
         {
            /* save scbNumber and est scb indicator in hhcb */
            hhcb->SCSI_HP_busHeldScbNumber = hiob->scbNumber;
            hhcb->SCSI_HP_estScb = hiob->SCSI_IP_estScb;

            /* Special handling for completing an IOB when the */
            /* SCSI bus is held.                               */
            /* The IOB is completed immediately instead of     */
            /* queueing in the DONE queue.                     */
            haStatus = hiob->haStat;
            nexus->SCSI_XP_hiobActive = 0;

            OSD_COMPLETE_SPECIAL_HIOB(hiob);
            if (hiob->stat == SCSI_TASK_CMD_COMP)
            {
               /* Task management command received */
               SCSIrTargetTaskManagementRequest(hhcb,haStatus,nexus);
            } 
         }
         else
         {
            if (!hiob->SCSI_IP_estScb)
            {
               /* free the scb */ 
               SCSIrFreeScb(hhcb,hiob);
            }
            else
            {
               /* return est scb to available pool */
               SCSI_rRETURNESTSCB(hhcb,hiob);
               if (!hhcb->SCSI_HF_targetHoldEstHiobs)
               {
                  /* An Est SCB is returned so see if we have an
                   * Est HIOB to queue to HWM layer.
                   */
                  if ((newHiob = SCSIrHiobPoolRemove(hhcb)) != SCSI_NULL_HIOB)
                  { 
                     SCSIrQueueEstHiobToHwm(hhcb,newHiob);
                  }
                } 
            }
            
            if (hiob->SCSI_IF_taskManagementResponse)
            {
               /* Task Management Response must call OSM post routine 
                  as an Interrupt is not generated */
               nexus->SCSI_XP_hiobActive = 0;
               if (nexus->SCSI_XP_deviceResetInProgress &&
                   (!hhcb->SCSI_HF_initiatorMode) &&
                   (hhcb->SCSI_HF_targetNumIDs <= 1))
               {
                  /* Attempt to requeue Establish Connection HIOBs
                   * to hardware layer.
                   * When operating in target mode only, and a
                   * Target Reset TMF is processed the 
                   * hardware is reset and all Establish HIOBs
                   * are aborted. Therefore, they need to be
                   * requeued somehow.
                   */
                  SCSI_rQUEUEESTHIOBS(hhcb);
               }                  
               OSD_COMPLETE_SPECIAL_HIOB(hiob);
            }  
            else
            {
               /* Put HIOB into Done queue - Add element at tail */
               hiob->queueNext = SCSI_NULL_HIOB;      
               if (hhcb->SCSI_DONE_TAIL==SCSI_NULL_HIOB)
               {
                  /* This is the 1st element. Init SCSI_DONE_HEAD accordingly */
                  hhcb->SCSI_DONE_HEAD = hiob;
               }
               else
               {
                  /* DoneQ already have at least 1 element */
                  hhcb->SCSI_DONE_TAIL->queueNext=hiob;
               }
               hhcb->SCSI_DONE_TAIL = hiob;
            }
         }
         return;

      case SCSI_CMD_ABORT_NEXUS:
         hhcb = SCSI_NEXUS_UNIT(hiob)->hhcb;
         nexus = SCSI_NEXUS_UNIT(hiob);
         /* Check if target mode bus held SCB to be freed. */ 
         SCSI_rFREEBUSHELDSCB(hhcb);

         /* Note that; order is important - if the HIOB in the host queue */
         /* was returned before calling the HWM then a HIMClearNexusTSH   */
         /* call in the post routine of the aborted HIOB would fail if    */
         /* the SCSI bus was held.                                          */
    
         /* Abort non-active HIOB(s) associated to the nexus  */
         /* in Host queue. */
         SCSIrFindAndAbortTargetHIOBs(hhcb, nexus, SCSI_HOST_ABT_HOST);
         SCSIrPostDoneQueue(hhcb);           /* Post back all HIOB(s) */
         break;

#if SCSI_MULTIPLEID_SUPPORT
      case SCSI_CMD_ENABLE_ID:
      case SCSI_CMD_DISABLE_ID:
         /* nothing required */
         break;
#endif /* SCSI_MULTIPLEID_SUPPORT */

#endif /* SCSI_TARGET_OPERATION */ 


      default:
         break;
   }

   /* Post back the completion of special HIOB */
   OSD_COMPLETE_SPECIAL_HIOB(hiob);
}

/*********************************************************************
*
*  SCSIrAsyncEvent
*
*     Asynchronous event notification
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 event code
*                 others...
*
*  Remarks:
*
*********************************************************************/
void SCSIrAsyncEvent (SCSI_HHCB SCSI_HPTR hhcb,
                      SCSI_UINT16 event, ...)
{

   switch (event)
   {
      case SCSI_AE_3PTY_RESET:
         /* All non-active HIOB(s) in Host queue and Device queues will */
         /* be aborted with the abort status caused by 3rd party reset */
         SCSIrClearHostQueue(hhcb, SCSI_HOST_ABT_3RD_RST);
         /* Check if target mode bus held SCB to be freed. */ 
         SCSI_rFREEBUSHELDSCB(hhcb); 
         SCSI_rQUEUEESTHIOBS(hhcb);
         /* The order of queues to process is important.
          * The PAC code will not terminate the PAC process if the 
          * completing I/O does not have an error that is fatal to this
          * process, such as, aborted due to reset. 
          * Therefore, to ensure the PAC process is terminated,
          * I/Os in the DONE queue are completed prior to aborting I/Os
          * in the device queues. This works as follows:
          * a) a PAC related I/O is in the DONE queue with a selection
          *    timeout result (for example)
          * b) this I/O completes and PAC queues an I/O to the next 
          *    device  
          * c) this new I/O then remains queued in the device queue as
          *    the queue is suspended due to the previous OSMFREEZE event.
          * d) the clear of the devices queues will result in the 
          *    termination of the PAC due to the I/O exception type.
          */
         SCSIrPostDoneQueue(hhcb);           /* Post back all HIOB(s) */
         SCSI_rCLEARALLDEVICEQUEUES(hhcb, SCSI_HOST_ABT_3RD_RST);
         break;

      case SCSI_AE_IOERROR:
         /* All non-active HIOB(s) in Host queue and Device queues will */
         /* be aborted with the abort status caused by io error */
         SCSIrClearHostQueue(hhcb, SCSI_HOST_ABT_IOERR);
         /* Check if target mode bus held SCB to be freed. */ 
         SCSI_rFREEBUSHELDSCB(hhcb); 
         SCSI_rQUEUEESTHIOBS(hhcb);
         /* The order of queues to process is important.
          * The PAC code will not terminate the PAC process if the 
          * completing I/O does not have an error that is fatal to this
          * process, such as, aborted due to reset. 
          * Therefore, to ensure the PAC process is terminated,
          * I/Os in the DONE queue are completed prior to aborting I/Os
          * in the device queues. This works as follows:
          * a) a PAC related I/O is in the DONE queue with a selection
          *    timeout result (for example)
          * b) this I/O completes and PAC queues an I/O to the next 
          *    device  
          * c) this new I/O then remains queued in the device queue as
          *    the queue is suspended due to the previous OSMFREEZE event.
          * d) the clear of the devices queues will result in the 
          *    termination of the PAC due to the I/O exception type.
          */
         SCSIrPostDoneQueue(hhcb);           /* Post back all HIOB(s) */
         SCSI_rCLEARALLDEVICEQUEUES(hhcb, SCSI_HOST_ABT_IOERR);
         break;

      case SCSI_AE_HA_RESET:
         /* All non-active HIOB(s) in Host queue and Device queues will */
         /* be aborted with the abort status caused by phase error */
         SCSIrClearHostQueue(hhcb, SCSI_HOST_PHASE_ERR);
         /* Check if target mode bus held SCB to be freed. */ 
         SCSI_rFREEBUSHELDSCB(hhcb); 
         SCSI_rQUEUEESTHIOBS(hhcb);
         /* The order of queues to process is important.
          * The PAC code will not terminate the PAC process if the 
          * completing I/O does not have an error that is fatal to this
          * process, such as, aborted due to reset. 
          * Therefore, to ensure the PAC process is terminated,
          * I/Os in the DONE queue are completed prior to aborting I/Os
          * in the device queues. This works as follows:
          * a) a PAC related I/O is in the DONE queue with a selection
          *    timeout result (for example)
          * b) this I/O completes and PAC queues an I/O to the next 
          *    device  
          * c) this new I/O then remains queued in the device queue as
          *    the queue is suspended due to the previous OSMFREEZE event.
          * d) the clear of the devices queues will result in the 
          *    termination of the PAC due to the I/O exception type.
          */
         SCSIrPostDoneQueue(hhcb);           /* Post back all HIOB(s) */
         SCSI_rCLEARALLDEVICEQUEUES(hhcb, SCSI_HOST_PHASE_ERR);
         break;

      case SCSI_AE_OSMFREEZE:
         /* Freeze RSM Layer */
         SCSIrFreezeHostQueue(hhcb,SCSI_FREEZE_EVENT);
         SCSI_rFREEZEALLDEVICEQUEUES(hhcb);
         break;

      case SCSI_AE_OSMUNFREEZE:
         /* Unfreeze RSM Layer */
         SCSIrUnFreezeHostQueue(hhcb,SCSI_FREEZE_EVENT); 
         SCSI_rUNFREEZEALLDEVICEQUEUES(hhcb,SCSI_FREEZE_MAPS);
         break;

#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING

      case SCSI_AE_ABORTED_PCI_ERROR:
         SCSIrClearHostQueue(hhcb, SCSI_HOST_ABORTED_PCI_ERROR);
         /* Check if target mode bus held SCB to be freed. */ 
         SCSI_rFREEBUSHELDSCB(hhcb); 
         SCSI_rQUEUEESTHIOBS(hhcb);
         /* The order of queues to process is important.
          * The PAC code will not terminate the PAC process if the 
          * completing I/O does not have an error that is fatal to this
          * process, such as, aborted due to reset. 
          * Therefore, to ensure the PAC process is terminated,
          * I/Os in the DONE queue are completed prior to aborting I/Os
          * in the device queues. This works as follows:
          * a) a PAC related I/O is in the DONE queue with a selection
          *    timeout result (for example)
          * b) this I/O completes and PAC queues an I/O to the next 
          *    device  
          * c) this new I/O then remains queued in the device queue as
          *    the queue is suspended due to the previous OSMFREEZE event.
          * d) the clear of the devices queues will result in the 
          *    termination of the PAC due to the I/O exception type.
          */
         SCSIrPostDoneQueue(hhcb);           /* Post back all HIOB(s) */
         SCSI_rCLEARALLDEVICEQUEUES(hhcb, SCSI_HOST_ABORTED_PCI_ERROR);
         break;
         
      case SCSI_AE_ABORTED_PCIX_SPLIT_ERROR:
         SCSIrClearHostQueue(hhcb, SCSI_HOST_ABORTED_PCIX_SPLIT_ERROR);
         /* Check if target mode bus held SCB to be freed. */ 
         SCSI_rFREEBUSHELDSCB(hhcb); 
         SCSI_rQUEUEESTHIOBS(hhcb);
         /* The order of queues to process is important.
          * The PAC code will not terminate the PAC process if the 
          * completing I/O does not have an error that is fatal to this
          * process, such as, aborted due to reset. 
          * Therefore, to ensure the PAC process is terminated,
          * I/Os in the DONE queue are completed prior to aborting I/Os
          * in the device queues. This works as follows:
          * a) a PAC related I/O is in the DONE queue with a selection
          *    timeout result (for example)
          * b) this I/O completes and PAC queues an I/O to the next 
          *    device  
          * c) this new I/O then remains queued in the device queue as
          *    the queue is suspended due to the previous OSMFREEZE event.
          * d) the clear of the devices queues will result in the 
          *    termination of the PAC due to the I/O exception type.
          */
         SCSIrPostDoneQueue(hhcb);           /* Post back all HIOB(s) */
         SCSI_rCLEARALLDEVICEQUEUES(hhcb, SCSI_HOST_ABORTED_PCIX_SPLIT_ERROR);
         break;
         
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */

#if SCSI_TARGET_OPERATION 
      case SCSI_AE_NEXUS_TSH_THRESHOLD:
      case SCSI_AE_EC_IOB_THRESHOLD:
         /* Do nothing */
         break;
#endif /* SCSI_TARGET_OPERATION */

      default:
         break;
   }

   /* AEN call to CHIM / XLM layer */
   OSD_ASYNC_EVENT(hhcb, event);

}

/*********************************************************************
*
*  SCSIrScsiBusReset
*
*     Process SCSI_Bus_Reset HIOB
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:       This routine will clear any HIOB(s) in Host queue
*                 and all Device queues.
*                  
*********************************************************************/
void SCSIrScsiBusReset (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_HHCB SCSI_HPTR hhcb = SCSI_INITIATOR_UNIT(hiob);

   /* Freeze RSM Layer */
   SCSI_rFREEZEALLDEVICEQUEUES(hhcb);

   /* Clear non-active HIOB(s) in Host queue and Device queues. */
   /* Start with the Host queue first. */
   SCSIrClearHostQueue(hhcb, SCSI_HOST_ABT_BUS_RST);

   /* The Device queues, if any */
   SCSI_rCLEARALLDEVICEQUEUES(hhcb, SCSI_HOST_ABT_BUS_RST);

   /* Pass SCSI_Bus_Reset HIOB down to Hardware layer */
   SCSIHQueueSpecialHIOB(hiob);
}

/*********************************************************************
*
*  SCSIrTargetReset
*
*     Process Target_Reset HIOB
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:       This routine will clear any HIOB(s) associated to
*                 particular target in Host queue and Device queues.
*                  
*********************************************************************/
void SCSIrTargetReset (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_HHCB SCSI_HPTR hhcb = targetUnit->hhcb;
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl = targetUnit->targetControl;

   /* Freeze the device queue if this is the first TR entering HWM layer. */
   if (!targetControl->activeTRs)
   {
      SCSIrFreezeDeviceQueue(targetControl, SCSI_FREEZE_TR);
   }

   /* Abort non-active HIOB(s) associated to the particular target  */
   /* in Host queue and Device queue.  Start with Host queue first. */
   if (hiob->cmd==SCSI_CMD_LOGICAL_UNIT_RESET)
   {
      SCSIrFindAndAbortHIOB(hhcb, targetUnit, SCSI_HOST_ABT_LUN_RST);
      /*Check device Q for matching lun*/
      SCSIrFindAndAbortDevQ(targetUnit, SCSI_HOST_ABT_LUN_RST);
      
   }
   else
   {
      SCSIrFindAndAbortHIOB(hhcb, targetUnit, SCSI_HOST_ABT_TRG_RST);
      /*Then Device Queue*/
      SCSIrClearDeviceQueue(targetControl, SCSI_HOST_ABT_TRG_RST);
      
   }
   
   /* Tracking number of active TR already sent to this target. */
   ++targetControl->activeTRs;
   
   /* Increment the number of active SCBs for this target. */
   /* TR does not need to qualify with the criteria.       */
   ++targetControl->activeScbs;

   /* Now check if SCB is available in FREE pool */
   if (SCSI_IS_NULL_SCB(hiob->scbNumber = SCSI_rGETSCB(hhcb,hiob)))
   {
      /* No SCB available, queue HIOB into Host queue */
      SCSIrHostQueueAppend(hhcb, hiob);
   }
   else
   {
      SCSIHQueueSpecialHIOB(hiob);
   }
}

/*********************************************************************
*
*  SCSIRPowerManagement
*
*     Execute power management function
*
*  Return Value:  0 - function execute successfully
*                 1 - function not supported
*                 2 - host device not idle         
*                  
*  Parameters:    hhcb
*                 powerMode
*
*  Remarks:       There should be no active hiob when this routine
*                 get called.
*                  
*********************************************************************/
SCSI_UINT8 SCSIRPowerManagement (SCSI_HHCB SCSI_HPTR hhcb,
                                 SCSI_UINT powerMode)
{
   if (SCSIRCheckHostIdle(hhcb) == SCSI_IDLE)
   {
      return(SCSIHPowerManagement(hhcb,powerMode));
   }
   else
   {
      return(2);
   }
}

/*********************************************************************
*
*  SCSIrAbortTask
*
*     Abort task associated
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:       There is no guarantee the aborted hiob is still
*                 in resource management layer.
*
*********************************************************************/
void SCSIrAbortTask (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_HIOB SCSI_IPTR hiobAbort = SCSI_RELATED_HIOB(hiob);

#if SCSI_TARGET_OPERATION
   if (hiobAbort->SCSI_IP_targetMode)
   {
      /* Only SCSI_CMD_ESTABLISH_CONNECTION target mode HIOBs may be
       * aborted.
       */
      /* Target mode HIOBs can only be in HIOB queue */
      if (SCSIrAbortHIOBInHiobQ(hiobAbort, SCSI_HOST_ABT_HOST))
      {
         /* Post back Abort_Task HIOB with successful completion */
         hiob->stat = SCSI_SCB_COMP;
         OSD_COMPLETE_SPECIAL_HIOB(hiob);
      }
      else
      {
         /* The aborting HIOB was not in the RSM layer. */
         /* So send down the Abort_Task HIOB to HWM layer. */
         SCSIHQueueSpecialHIOB(hiob);
      }
   }
   else
#endif /* SCSI_TARGET_OPERATION */
   if (SCSIrAbortHIOBInHostQ(hiobAbort, SCSI_HOST_ABT_HOST) ||
       SCSIrAbortHIOBInDevQ(hiobAbort, SCSI_HOST_ABT_HOST))   
   {
      /* Post back Abort_Task HIOB with successful completion */
      hiob->stat = SCSI_SCB_COMP;
      OSD_COMPLETE_SPECIAL_HIOB(hiob);
   }
   else
   {
      /* The aborting HIOB was not in the RSM layer. */
      /* So send down the Abort_Task HIOB to HWM layer. */
      SCSIHQueueSpecialHIOB(hiob);
   }
}

/*********************************************************************
*
*  SCSIrAbortTaskSet
*
*     Abort task set associated
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:       There is no guarantee the aborted hiob is still
*                 in resource management layer.
*
*********************************************************************/
void SCSIrAbortTaskSet (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_HHCB SCSI_HPTR hhcb = targetUnit->hhcb;
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl = targetUnit->targetControl;

   /* Freeze the device queue if this is the first Abort_Task_Set */
   /* entering HWM layer. */
   if (!targetControl->activeABTTSKSETs)
   {
      SCSIrFreezeDeviceQueue(targetControl, SCSI_FREEZE_ABTTSKSET);
   }

   /* Abort non-active HIOB(s) associated to the target's task set  */
   /* in Host queue and Device queue.  Start with Host queue first. */
   SCSIrFindAndAbortHIOB(hhcb, targetUnit, SCSI_HOST_ABT_HOST);

   /* Then Device queue ... */
   SCSIrClearDeviceQueue(targetControl, SCSI_HOST_ABT_HOST);

   /* Tracking number of active Abort_Task_Set already sent to this target. */
   ++targetControl->activeABTTSKSETs;
   
   /* Send request down to HWM layer */
   SCSIHQueueSpecialHIOB(hiob);
}

/*********************************************************************
*
*  SCSIrResetHardware
*
*     Reset Harpoon harware
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:       The decision to actually issue a hardware reset is 
*                 dependent upon the presence of a PCI or PCI-X error
*                 inprogress.
*
*********************************************************************/
void SCSIrResetHardware (SCSI_HIOB SCSI_IPTR hiob)
{
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING      
   /* This is part of the PCI/PCI-X recovery. 
      Send this down to the HWM layer. */
   SCSIHQueueSpecialHIOB(hiob);
#else      
   /* Post back Abort_Task HIOB with successful completion */
   hiob->stat = SCSI_SCB_COMP;
   OSD_COMPLETE_SPECIAL_HIOB(hiob);
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */
}

/*********************************************************************
*
*  SCSIRGetHWInformation
*
*     Gets information from the hardware to put into the 
*  SCSI_HW_INFORMATION structure.  An example usage is to update 
*  adapter/target profile information for the CHIM interface.
*
*  Return Value:  none.
*                  
*  Parameters:    hwInfo      - SCSI_HW_INFORMATION structure.
*                 hhcb        - HHCB structure for the adapter.
*
*  Remarks:       None.
*                 
*********************************************************************/
#if SCSI_PROFILE_INFORMATION
void SCSIRGetHWInformation (SCSI_HW_INFORMATION SCSI_LPTR hwInfo, 
                            SCSI_HHCB SCSI_HPTR hhcb)                  
{
   SCSI_INT i;
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl;

   for (i = 0,targetControl = hhcb->SCSI_HP_targetControl; i < SCSI_MAXDEV;
        i++,targetControl++)
   {
      /* If the device queue is frozen ... */
      if (targetControl->freezeMap)
      {
         /* ... Then get the value from the origMaxTagScbs. */
         hwInfo->targetInfo[i].maxTagScbs = targetControl->origMaxTagScbs;
      }
      else
      {
         /* Otherwise, get it from the maxTagScbs. */
         hwInfo->targetInfo[i].maxTagScbs = targetControl->maxTagScbs;
      }
   }
   
   SCSIHGetHWInformation(hwInfo, hhcb);
}
#endif /* SCSI_PROFILE_INFORMATION */

/*********************************************************************
*
*  SCSIRPutHWInformation
*
*     Updates the the hardware with information from the SCSI_HW_INFORMATION
*  structure.  An example usage is to update adapter/target profile
*  information for the CHIM interface.
*
*  Return Value:  none.
*                  
*  Parameters:    hwInfo      - SCSI_HW_INFORMATION structure.
*                 hhcb        - HHCB structure for the adapter.
*
*  Remarks:       None.
*                 
*********************************************************************/
#if (SCSI_PROFILE_INFORMATION && (!SCSI_DISABLE_ADJUST_TARGET_PROFILE))
void SCSIRPutHWInformation (SCSI_HW_INFORMATION SCSI_LPTR hwInfo, 
                            SCSI_HHCB SCSI_HPTR hhcb)                  
{
   SCSI_INT i;
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl;

   for (i = 0,targetControl = hhcb->SCSI_HP_targetControl; i < SCSI_MAXDEV;
        i++,targetControl++)
   {
      /* If the device queue is frozen ... */
      if (targetControl->freezeMap)
      {
         /* ... Then put the value into the origMaxTagScbs. */
         targetControl->origMaxTagScbs = hwInfo->targetInfo[i].maxTagScbs;
      }
      else
      {
         /* Otherwise, put it into the maxTagScbs. */
         targetControl->maxTagScbs = hwInfo->targetInfo[i].maxTagScbs;
      }
   }
   
   SCSIHPutHWInformation(hwInfo, hhcb);
}
#endif /* (SCSI_PROFILE_INFORMATION && (!SCSI_DISABLE_ADJUST_TARGET_PROFILE)) */

/*********************************************************************
*
*  SCSIrAsyncEventCommand
*
*     Asynchronous event notification for a command
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 event code
*                 others...
*
*  Remarks:
*
*********************************************************************/
void SCSIrAsyncEventCommand (SCSI_HHCB SCSI_HPTR hhcb,
                             SCSI_HIOB SCSI_IPTR hiob,
                             SCSI_UINT16 event, ...)
{
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl;
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_HIOB SCSI_IPTR hiobCurr;
   SCSI_HIOB SCSI_IPTR hiobPrev;
   SCSI_UEXACT8 scsiId;

   switch (event)
   {
      case SCSI_AE_FREEZEONERROR_START:
#if SCSI_PACKETIZED_IO_SUPPORT
      case SCSI_AE_PACKTONONPACK_DETECTED:
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
         targetControl = targetUnit->targetControl;

         /* Save the number of active SCBs */
         targetControl->origActiveScbs = targetControl->activeScbs;

         /* Freeze the device queue */
         SCSIrFreezeDeviceQueue(targetControl, SCSI_FREEZE_ERRORBUSY);

         /* Remember the Device Queue Tail, as the queue has to be    */
         /* adjusted after all the HIOBs with SCSI_SCB_FROZEN status  */
         /* have been returned by the hardware layer.                 */
         targetControl->freezeTail = targetControl->queueTail;

         /* Get the target id */
         scsiId = (SCSI_UEXACT8) targetUnit->scsiID;

         /* Remove HIOBs for this target from the host queue */
         targetControl->freezeHostHead = targetControl->freezeHostTail =
                    SCSI_NULL_HIOB;
         hiobPrev = hiobCurr = hhcb->SCSI_WAIT_HEAD;

         while (hiobCurr != SCSI_NULL_HIOB)
         {
            if ((SCSI_UEXACT8)(SCSI_TARGET_UNIT(hiobCurr)->scsiID) == scsiId)
            {
               /*
                * Found an hiob that will be moved from HostQ into 
                * DeviceQ. Adjust number of HIOBs in device queue.
                */
               targetControl->deviceQueueHiobs++;
            
               /* Remove the hiob from the HostQ */
               
               if (hiobPrev == hhcb->SCSI_WAIT_TAIL)
               {
                  /* HostQ is going to be empty */
                  hhcb->SCSI_WAIT_HEAD = hhcb->SCSI_WAIT_TAIL = SCSI_NULL_HIOB;
               }
               else
               {
                  /* HostQ won't be empty */

                  if (hhcb->SCSI_WAIT_HEAD==hiobCurr)
                  {
                     /*
                      * HostQ has > 1 hiob. The 1st hiob is being removed,
                      * hence hiobPrev & SCSI_WAIT_HEAD should be pointing to 
                      * the next element in the list.
                      */
                     hhcb->SCSI_WAIT_HEAD = hiobCurr->queueNext;
                     hiobPrev = hiobCurr->queueNext;
                  }
                  else
                  {
                     /*
                      * An hiob in the middle of HostQ is being removed.
                      * Adjust previousElement's next pointer.
                      */
                     hiobPrev->queueNext = hiobCurr->queueNext;
                  }
               }

               /* Insert the hiob into the temporary host queue used during */
               /* the freeze logic.                                         */
               if (targetControl->freezeHostHead == SCSI_NULL_HIOB)
               {
                  targetControl->freezeHostHead =
                     targetControl->freezeHostTail = hiobCurr;
               }
               else
               {
                  targetControl->freezeHostTail->queueNext = hiobCurr;
                  targetControl->freezeHostTail = hiobCurr;
               }

#if SCSI_NEGOTIATION_PER_IOB
               /* If the hiob has any of the 'forceNego' flags set, increment */
               /* the pendingNego field to indicate the one more negotiation  */
               /* is needed. */
               if (hiob->SCSI_IF_forceSync || hiob->SCSI_IF_forceAsync ||
                   hiob->SCSI_IF_forceWide || hiob->SCSI_IF_forceNarrow)
               {
                  targetControl->pendingNego++;
               }
#endif /* SCSI_NEGOTIATION_PER_IOB */
            }
            else
            {
               hiobPrev = hiobCurr;
            }
            
            /* Continue to check next hiob in HostQ */
            hiobCurr = hiobCurr->queueNext;
         }
         
         break;
      case SCSI_AE_FREEZEONERROR_END:
#if SCSI_AE_PACKTONONPACK_END
      case SCSI_AE_PACKTONONPACK_END:
#endif /* SCSI_AE_PACKTONONPACK_END */
         targetControl = targetUnit->targetControl;

         /* Link the temporary host queue (which was built during   */
         /* FREEZEONERROR_START) behind the device queue            */
         if (targetControl->freezeHostTail != SCSI_NULL_HIOB)
         {
            targetControl->queueTail->queueNext = targetControl->freezeHostHead;
            targetControl->queueTail = targetControl->freezeHostTail;
         }

         /* Mend the device queue in such a way that the chronological order */
         /* of the commands are maintained                                   */
         if (targetControl->freezeTail != SCSI_NULL_HIOB)
         {
            targetControl->queueTail->queueNext = targetControl->queueHead;
            targetControl->queueHead = targetControl->freezeTail->queueNext;
            targetControl->freezeTail->queueNext = SCSI_NULL_HIOB;
            targetControl->queueTail = targetControl->freezeTail;
            targetControl->freezeTail = SCSI_NULL_HIOB;
         }

         /* Restore the number of active SCBs */
         targetControl->activeScbs = targetControl->origActiveScbs;
#if SCSI_AE_PACKTONONPACK_END
         if (event == SCSI_AE_PACKTONONPACK_END)
         {
#if SCSI_PACKETIZED_IO_SUPPORT
            /*Unfreeze in case iob may have forced switch because*/
            /* of no auto sense or tag enable in IU mode.*/  
            SCSIrUnFreezeDeviceQueue(SCSI_TARGET_UNIT(hiob)->targetControl,
                                  SCSI_FREEZE_PACKSWITCH);
#endif /*SCSI_PACKETIZED_IO_SUPPORT*/
            /* Unfreeze the device queue and dispatch any queued HIOBs */
            /* if the queue is not frozen. */
            SCSIrUnFreezeDeviceQueue(SCSI_TARGET_UNIT(hiob)->targetControl,
                                  SCSI_FREEZE_ERRORBUSY);
            SCSIrDispatchQueuedHIOB(hiob);
         }
#endif /* SCSI_AE_PACKTONONPACK_END */        
         break;

#if (SCSI_PACKETIZED_IO_SUPPORT && SCSI_AE_NONPACKTOPACK_END)
      case SCSI_AE_NONPACKTOPACK_END:

            /*unfreeze for any potential new qiob that needed another switch*/  
            SCSIrUnFreezeDeviceQueue(SCSI_TARGET_UNIT(hiob)->targetControl,
                                  SCSI_FREEZE_PACKSWITCH);
            /* Unfreeze the device queue and dispatch any queued HIOBs */
            /* if the queue is not frozen. */
            SCSIrDispatchQueuedHIOB(hiob);
         break;
#endif /*SCSI_PACKETIZED_IO_SUPPORT && SCSI_AE_NONPACKTOPACK_END*/

      default:
         break;
   }
}

/*********************************************************************
*
*  SCSIrSearchDoneQForDVRequest
*
*     Search Done Q for a Domain Validation HIOB  
*
*  Return Value:  hiob - Either Domain Validation HIOB or
*                        SCSI_NULL_HIOB  
*                  
*  Parameters:    hhcb
*
*  Remarks:       The HIOB is not removed from the DONE Q.
*
*********************************************************************/
#if SCSI_DOMAIN_VALIDATION
SCSI_HIOB SCSI_IPTR SCSIRSearchDoneQForDVRequest (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_HIOB SCSI_IPTR hiob;

   hiob = hhcb->SCSI_DONE_HEAD;

   /* Search DoneQ */
   while (hiob != SCSI_NULL_HIOB)
   {
      if (hiob->SCSI_IF_dvIOB)
      {
         /* Found DV request */ 
         break;
      }
      hiob = hiob->queueNext;
   }

   return(hiob);
}
#endif /* SCSI_DOMAIN_VALIDATION */

/*********************************************************************
*
*  SCSIrHiobPoolRemove
*
*     Get an Establish Connection IOB from the hiobQueue  
*
*  Return Value:  hiob
*                  
*  Parameters:    hhcb
*
*  Remarks:
*
*********************************************************************/
#if SCSI_TARGET_OPERATION
SCSI_HIOB SCSI_IPTR  SCSIrHiobPoolRemove (SCSI_HHCB SCSI_HPTR hhcb)
{ 
   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_UEXACT16 queueCnt = hhcb->SCSI_HF_targetHiobQueueCnt; 

   OSDDebugPrint(8,("\nEntered SCSIrHiobPoolRemove "));

   if (queueCnt == 0 )
   { 
      /* send Alert to OSM?? */
      return( SCSI_NULL_HIOB );
   }
    
   hiob = hhcb->SCSI_HF_targetHiobQueue; 
   hhcb->SCSI_HF_targetHiobQueue = hiob->queueNext;
    
   hhcb->SCSI_HF_targetHiobQueueCnt = --queueCnt;
    
   if (hhcb->SCSI_HF_targetHiobQueueThreshold)
   {
      /* Check if OSMEvent to be sent */   
      if (queueCnt == hhcb->SCSI_HF_targetHiobQueueThreshold)
      {
         /* send a Alert to OSM */
         SCSIrAsyncEvent(hhcb,SCSI_AE_EC_IOB_THRESHOLD);
      }
   }

   return(hiob);
}
#endif /* SCSI_TARGET_OPERATION */
              
/*********************************************************************
*
*  SCSIrAllocateNexus
*
*     Get a nexus (SCSI_NEXUS structure) from the nexus pool.  
*
*  Return Value:  nexus
*                  
*  Parameters:    hhcb
*
*  Remarks:
*
*********************************************************************/
#if SCSI_TARGET_OPERATION
SCSI_NEXUS SCSI_XPTR SCSIrAllocateNexus (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_NEXUS SCSI_XPTR nexus;
   SCSI_UEXACT16 queueCnt = hhcb->SCSI_HF_targetNexusQueueCnt;
         
   OSDDebugPrint(8,("\nEntered SCSIrAllocateNexus "));

   if (queueCnt == 0)
   {
     /* send alert to OSM - or we should never get here?? */
     return(SCSI_NULL_NEXUS);
   }
   
   hhcb->SCSI_HF_targetNexusQueueCnt = --queueCnt;
   
   if (hhcb->SCSI_HF_targetNexusThreshold)
   {
      if (queueCnt == hhcb->SCSI_HF_targetNexusThreshold)
      {
         /* Send alert to OSM */
         SCSIrAsyncEvent(hhcb,SCSI_AE_NEXUS_TSH_THRESHOLD);                                    
      }
   } 
   nexus = hhcb->SCSI_HF_targetNexusQueue;
   hhcb->SCSI_HF_targetNexusQueue = nexus->nextNexus;
  
   /* Nexus in use now */
   SCSI_rFRESH_NEXUS(nexus);

   return(nexus);
     
}
#endif /* SCSI_TARGET_OPERATION */

/*********************************************************************
*
*  SCSIrFreeNexus
*
*     Return a Nexus (SCSI_NEXUS) to the available nexus queue
*
*  Return Value:  none
*                  
*  Parameters:    hhcb, nexus
*
*  Remarks:
*
*********************************************************************/
#if SCSI_TARGET_OPERATION
void SCSIrFreeNexus (SCSI_HHCB SCSI_HPTR hhcb, SCSI_NEXUS SCSI_XPTR nexus)
{
   /* always add to the head of the queue (we don't care about order) */     
   nexus->SCSI_XF_available = 1;
   nexus->nextNexus = hhcb->SCSI_HF_targetNexusQueue;
   hhcb->SCSI_HF_targetNexusQueue = nexus;
   hhcb->SCSI_HF_targetNexusQueueCnt++;
}
#endif /* SCSI_TARGET_OPERATION */

/*********************************************************************
*
*  SCSIrClearNexus
*
*     Clear a Nexus
*
*  Return Value:  0 - nexus cleared or nexus already
*                     cleared (i.e. returned to the available nexus pool)
*                 1 - I/O Bus held, nexus NOT cleared  
*                 2 - nexus was not idle, HIOBs pending,                   nexus NOT cleared  
*                  
*  Parameters:    nexus, hhcb
*
*  Remarks:       Returns the nexus to the available nexus pool, if and
*                 only if, there are no HIOBs active for this nexus, and
*                 the SCSI bus is NOT held by this nexus.
*                 If the nexus is already in the available pool then return 
*                 a good result (not an error). 
*
*********************************************************************/
#if SCSI_TARGET_OPERATION
SCSI_UINT8 SCSIrClearNexus (SCSI_NEXUS SCSI_XPTR nexus, SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_UINT8 result = 0;

   if (nexus->SCSI_XF_available)
   {
      /* nexus is in available pool - just return successful result */
   }
   else if (nexus->SCSI_XP_hiobActive)
   {
      /* HIOBs pending */
      result = 2;
   }
   else if (SCSIHTargetClearNexus(nexus,hhcb))
   {
      /* I/O Bus held by this nexus */
      result = 1;
   }
   else
   {
      /* Return the nexus to the available pool */ 
      SCSIrFreeNexus(hhcb,nexus);
   }
  
   /* Return HIM_SUCCESS if nexus cleared. */ 
   return(result);
   
}
#endif /* SCSI_TARGET_OPERATION */
                         
/*********************************************************************
*
*  SCSIrQueueEstHiobPool
*
*     Queue an Establish Connection hiob to the Establish hiob pool.
*
*  Return Value:  none
*                  
*  Parameters:    hiob, hhcb
*
*  Remarks:
*
*********************************************************************/
#if SCSI_TARGET_OPERATION
void SCSIrQueueEstHiobPool (SCSI_HHCB SCSI_HPTR hhcb,
                            SCSI_HIOB SCSI_IPTR hiob)
{
   OSDDebugPrint(8,("\nEntered SCSIrQueueEstHiobPool "));

   /* always add to the head of the queue (we don't care about order) */                                 
   hiob->queueNext = hhcb->SCSI_HF_targetHiobQueue;
   hhcb->SCSI_HF_targetHiobQueue = hiob; 
   hhcb->SCSI_HF_targetHiobQueueCnt++;
   return; 
}
#endif /* SCSI_TARGET_OPERATION */

/*********************************************************************
*
*  SCSIrQueueEstHiobToHwm
*
*     Queue an Establish Connection HIOB to the HWM layer, if
*     resources are available, otherwise queue to the pool of
*     available HIOBs (Establish Connection HIOBs).
*
*  Return Value:  none
*                  
*  Parameters:    hiob, hhcb
*
*  Remarks:
*
*********************************************************************/
#if SCSI_TARGET_OPERATION
void SCSIrQueueEstHiobToHwm (SCSI_HHCB SCSI_HPTR hhcb,
                             SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_NEXUS SCSI_XPTR nexusUnit;
   
   OSDDebugPrint(8,("\nEntered SCSIrQueueEstHiobToHwm "));

   /* get the Nexus & establish connection scb */
   if ((nexusUnit = SCSIrAllocateNexus(hhcb)) != SCSI_NULL_NEXUS) 
   {
      if (!SCSI_IS_NULL_SCB(hiob->scbNumber = SCSI_rGETESTSCB(hhcb,hiob)))
      {
         SCSI_NEXUS_UNIT(hiob) = nexusUnit;
         /* Indicate that hiob is queued to HWM layer */
         hiob->SCSI_IP_estQueuedToHwm = 1;
         SCSIHQueueSpecialHIOB(hiob);
         return;
      }
      else
      {
         /* free the nexus */
         SCSIrFreeNexus(hhcb,nexusUnit);
      }
   }
   /* Queue to Est HIOB queue */
   SCSIrQueueEstHiobPool(hhcb,hiob);  
   return; 
}
#endif /* SCSI_TARGET_OPERATION */

/*********************************************************************
*
*  SCSIrAllocateNode
*
*     Get a node (SCSI_NODE structure) from the node pool.  
*
*  Return Value:  node
*                  
*  Parameters:    hhcb
*
*  Remarks:
*
*********************************************************************/
#if SCSI_TARGET_OPERATION
SCSI_NODE SCSI_NPTR SCSIrAllocateNode (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_NODE SCSI_NPTR node;
   SCSI_UEXACT16 queueCnt = hhcb->SCSI_HF_targetNexusQueueCnt;
         
   node = hhcb->SCSI_HF_targetNodeQueue;
   if (node != SCSI_NULL_NODE)
   {
      hhcb->SCSI_HF_targetNodeQueue = node->nextNode;
   }
   
   return(node);
     
}
#endif /* SCSI_TARGET_OPERATION */

/*********************************************************************
*
*  SCSIrTargetTaskManagementRequest
*
*     Process a Task Management request received from an initiator  
*
*  Return Value:  none
*                  
*  Parameters:    hhcb, haStatus, nexus
*
*  Remarks:  This routine assumes that information in nexus is valid
*            even though the IOB associated with this nexus has been
*            returned to the OSM and the OSM may have issued a 
*            HIMClearNexus. I.e. HIMClearNexus must NOT reset any 
*            connection information fields in the nexus.
*
*********************************************************************/
#if SCSI_TARGET_OPERATION
void SCSIrTargetTaskManagementRequest (SCSI_HHCB SCSI_HPTR hhcb,
                                       SCSI_UEXACT8 haStatus,
                                       SCSI_NEXUS SCSI_XPTR nexus)
{    
        
   switch (haStatus)
   {
      case SCSI_HOST_TARGET_RESET:
         SCSIrFindAndAbortTargetHIOBs(hhcb,SCSI_NULL_NEXUS,haStatus);
         break;
             
      case SCSI_HOST_ABORT_TASK_SET:
      case SCSI_HOST_ABORT_TASK:
      case SCSI_HOST_CLEAR_TASK_SET:
      case SCSI_HOST_LUN_RESET:
         /* These requests involve aborting HIOBs based on */
         /* nexus information */ 
         SCSIrFindAndAbortTargetHIOBs(hhcb,nexus,haStatus);
         break;

      case SCSI_HOST_CLEAR_ACA:
         /* SCSI-3 Clear ACA */
      case SCSI_HOST_TERMINATE_TASK:
         /* SCSI-2 Terminate I/O Process or SCSI-3 Terminate Task */

         /* Do nothing */
         break;
      
      default:
         /* Should never get here */
         break;

   }
}
#endif /* SCSI_TARGET_OPERATION */




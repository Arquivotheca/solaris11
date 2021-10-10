/*$Header: /vobs/u320chim/src/chim/rsm/rsmutil.c   /main/51   Fri Aug 22 16:19:19 2003   hu11135 $*/

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
*  Module Name:   RSMUTIL.C
*
*  Description:
*                 Utilities for resource management layer
*
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         NONE
*
*  Entry Point(s):
*
***************************************************************************/

#include "scsi.h"
#include "rsm.h"

/*********************************************************************
*
*  SCSIrCheckCriteria
*
*     Continue scb processing with checking criteria
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       This routine will check if max tags/nontags
*                 criteria meet. If it's meet then continue
*                 HIOB execution process else just queue it.
*                  
*********************************************************************/
#if !SCSI_STREAMLINE_QIOPATH
void SCSIrCheckCriteria (SCSI_HHCB SCSI_HPTR hhcb,
                         SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl = SCSI_TARGET_UNIT(hiob)->targetControl;
   SCSI_UINT16 maxScbs;

   /* get the criteria factor */
   maxScbs = ((hiob->SCSI_IF_tagEnable) ? targetControl->maxTagScbs :
                                       targetControl->maxNonTagScbs);
#if SCSI_NEGOTIATION_PER_IOB
   /* If HIOB sets the flags to force negotiation, try to do the negotiation  */
   if (hiob->SCSI_IF_forceSync || hiob->SCSI_IF_forceAsync ||
       hiob->SCSI_IF_forceWide || hiob->SCSI_IF_forceNarrow)
   {
      /* If the device is not idle, simply put the hiob into the device queue */
      /* and remember the fact that negotiation is pending, else change the   */
      /* xfer option parameters to do the negotiation on the next command.    */
      if (!SCSIHCheckDeviceIdle(hhcb, SCSI_TARGET_UNIT(hiob)->scsiID))
      {
         maxScbs = 0;
      }
      else
      {
         SCSI_hCHANGEXFEROPTION(hiob);
      }
   }
   /* If a negotiation is pending, queue the incoming hiob into the device    */
   /* queue, so that the command associated with this hiob will be executed   */
   /* after the negotiation.  */
   if (targetControl->pendingNego)
   {
      maxScbs = 0;
   }
#endif /* SCSI_NEGOTIATION_PER_IOB */

#if SCSI_PACKETIZED_IO_SUPPORT        
   /* Check for tagged command and auto-sense flag is enable and suppressNego */
   /* flag is disable and either switch packetized progress flag is on or     */
   /* initially negotiate for packetized flag is on, then set flag for target */
   /* to switch to packetized mode.  Check if we are not frozen (maxScbs > 0).*/
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
         /* Freeze if switch can't happen, not idle */
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

   /* check, if not frozen, for target in packetized mode, and auto-sense*/
   /*   is disable. Set flag to switch to non-packetized mode */  
   if (SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable) && maxScbs &&
       (!hiob->SCSI_IF_tagEnable         
#if (SCSI_TASK_SWITCH_SUPPORT == 1)                    
       || !hiob->SCSI_IF_autoSense 
#endif /* SCSI_TASK_SWITCH_SUPPORT == 1 */          
       ))
   { 
      if (!SCSIrSwitchToNonPacketized (hiob->unitHandle.targetUnit))
      {
         /* Freeze if switch can't happen, not idle */
         SCSIrFreezeDeviceQueue(targetControl,SCSI_FREEZE_PACKSWITCH);
         maxScbs = 0;
      }
   }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */ 
   if (++targetControl->activeScbs <= maxScbs)
   {
      /* criteria met, continue with allocating scb */
      SCSIrAllocateScb(hhcb,hiob);
   }
   else if ((targetControl->freezeMap) && (hiob->priority >= 0x80))
   {
#if SCSI_PACKETIZED_IO_SUPPORT           
#if (SCSI_TASK_SWITCH_SUPPORT == 2)
      if (SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable) && 
          ((SCSI_UEXACT8)OSDGetTargetCmd(hiob) == 0x03)) 
      {
         OSDmemcpy ((SCSI_UEXACT8 HIM_PTR) hiob->snsBuffer,
                   (SCSI_UEXACT8 HIM_PTR) SCSI_TARGET_UNIT(hiob)->senseBuffer, 
                    hiob->snsLength );
                           
         /* process successful status */
         hiob->stat = SCSI_SCB_COMP;            
         OSD_COMPLETE_HIOB(hiob);                   
         return;                         
      }                        
#endif  /* SCSI_TASK_SWITCH_SUPPORT == 2 */
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
}
#endif /* !SCSI_STREAMLINE_QIOPATH */

/*********************************************************************
*
*  SCSIrAllocateScb
*
*     Continue scb processing with allocating scb
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       This routine will try to allocate scb for
*                 specified hiob. if it's available then 
*                 continue HIOB execution process else just
*                 queue it.
*                  
*********************************************************************/
void SCSIrAllocateScb (SCSI_HHCB SCSI_HPTR hhcb,
                       SCSI_HIOB SCSI_IPTR hiob)
{
   if (!SCSI_IS_NULL_SCB(hiob->scbNumber = SCSI_rGETSCB(hhcb,hiob)))
   {
      /* scb available, queue to hardware management layer */
      SCSIHQueueHIOB(hiob);
   }
   else
   {
      /* queue it to host queue */
      SCSIrHostQueueAppend(hhcb,hiob);
   }
}

/*********************************************************************
*
*  SCSIrGetScb
*
*     Get free scb if available
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
SCSI_UEXACT16 SCSIrGetScb (SCSI_HHCB SCSI_HPTR hhcb,
                           SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_UEXACT16 scbNumber;
#if (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION)
   SCSI_UEXACT8 getPacketizedScbNumber;
#endif /* (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION) */

   SCSI_SET_NULL_SCB(scbNumber);

#if (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION)
   /* Initialize to assume skip getting packetized SCB number. */ 
   getPacketizedScbNumber = 0;

#if SCSI_TARGET_OPERATION
   if ((hhcb->SCSI_HF_targetMode) &&
       (!hhcb->SCSI_NON_EST_SCBS_AVAIL) &&
       (hiob->cmd != SCSI_CMD_ESTABLISH_CONNECTION))
   {
      /* If the non establish SCB count is 0 and the 
       * request is for a non establish connection HIOB
       * then return NULL SCB.
       */
       return (scbNumber);
   }

   if (hhcb->numberScbs > (SCSI_UEXACT16)256)
   {
      /* Possible packetized SCB number available. */
      if (hhcb->SCSI_HF_targetMode &&
          hiob->SCSI_IP_targetMode)
      {
         /* Target mode request may used packetized SCB number. */
         getPacketizedScbNumber = 1;
      }
#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT)
      else
      {
         /* Initiator mode HIOB. */
         if (SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))
         {
            /* Only get packetized SCB if operating in packetized mode. */
            getPacketizedScbNumber = 1;
         }
      }
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT) */
   }   
#else /* !SCSI_TARGET_OPERATION */
   if ((hhcb->numberScbs > (SCSI_UEXACT16)256) &&
       SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))
   {
      getPacketizedScbNumber = 1;
   }
#endif /* SCSI_TARGET_OPERATION */
    
   /* Allocate the SCB number from the packetized stack if it exists and the */
   /* I/O is for packetized target. The packetized stack exists only if the  */
   /* total number of SCBs allocated is more than 256. */
   if (getPacketizedScbNumber)
   {
      scbNumber =
         hhcb->SCSI_FREE_PACKETIZED_STACK[hhcb->SCSI_FREE_PACKETIZED_PTR];
      SCSI_SET_NULL_SCB(
         hhcb->SCSI_FREE_PACKETIZED_STACK[hhcb->SCSI_FREE_PACKETIZED_PTR]);
      hhcb->SCSI_FREE_PACKETIZED_PTR =
         (SCSI_UEXACT8)
         (((SCSI_UEXACT16)(++(hhcb->SCSI_FREE_PACKETIZED_PTR))) %
         (hhcb->numberScbs - (SCSI_UEXACT16)256));
   }

   /* If the packetized stack is empty or does not exist, or the I/O is for  */
   /* non-packetized target, allocate the SCB number from the non-packetized */
   /* stack. */
   if (SCSI_IS_NULL_SCB(scbNumber))
   {
      scbNumber = 
       hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR];
      SCSI_SET_NULL_SCB(
       hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR]);
      hhcb->SCSI_FREE_NON_PACKETIZED_PTR =
         (SCSI_UEXACT8)
         (((SCSI_UEXACT16)(++(hhcb->SCSI_FREE_NON_PACKETIZED_PTR))) %
         hhcb->numberScbs);
   }
#else /* !SCSI_PACKETIZED_IO_SUPPORT */
   /* Allocate the SCB number from the non-packetized  stack. */
   scbNumber = 
      hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR];
   SCSI_SET_NULL_SCB(
      hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR]);
   hhcb->SCSI_FREE_NON_PACKETIZED_PTR =
      (SCSI_UEXACT8)
      (((SCSI_UEXACT16)(++(hhcb->SCSI_FREE_NON_PACKETIZED_PTR))) %
      hhcb->numberScbs);
#endif /* (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION) */

#if SCSI_TARGET_OPERATION
   if (!SCSI_IS_NULL_SCB(scbNumber))
   { 
      /* Decrement available count */
      SCSI_rDECREMENT_NON_EST_SCBS_AVAIL(hhcb);
   }
#endif /* SCSI_TARGET_OPERATION */

   return (scbNumber);
}

/*********************************************************************
*
*  SCSIrReturnScb
*
*     Standard mode return scb to free pool
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 scb number to be returned
*
*  Remarks:
*                  
*********************************************************************/
void SCSIrReturnScb (SCSI_HHCB SCSI_HPTR hhcb,
                     SCSI_HIOB SCSI_IPTR hiob)
{
#if (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION)
#if SCSI_RAID1 

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
#endif /* SCSI_RAID1 */

   /* Return the SCB number to the appropriate STACK */
   if (hiob->scbNumber >= (SCSI_UEXACT16)256)
   {
      if ((--hhcb->SCSI_FREE_PACKETIZED_PTR) == (SCSI_UEXACT8)0xFF)
      {
         hhcb->SCSI_FREE_PACKETIZED_PTR =
            (SCSI_UEXACT8)((hhcb->numberScbs - 1) - (SCSI_UEXACT16)256);
      }
      hhcb->SCSI_FREE_PACKETIZED_STACK[hhcb->SCSI_FREE_PACKETIZED_PTR] =
         hiob->scbNumber;
   }
   else
   {
      if (((--hhcb->SCSI_FREE_NON_PACKETIZED_PTR) == (SCSI_UEXACT8)0xFF) &&
          (hhcb->numberScbs < (SCSI_UEXACT16)256))
      {
         hhcb->SCSI_FREE_NON_PACKETIZED_PTR =
            (SCSI_UEXACT8)(hhcb->numberScbs - 1);
      }
      hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR] =
         hiob->scbNumber;
   }
#else /* !SCSI_PACKETIZED_IO_SUPPORT */
#if SCSI_RAID1
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
#endif /* SCSI_RAID1 */

   /* Return the SCB number to the non-packetized STACK */
   if (((--hhcb->SCSI_FREE_NON_PACKETIZED_PTR) == (SCSI_UEXACT8)0xFF) &&
       (hhcb->numberScbs < (SCSI_UEXACT16)256))
   {
      hhcb->SCSI_FREE_NON_PACKETIZED_PTR = (SCSI_UEXACT8)(hhcb->numberScbs - 1);
   }
   hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR] =
      hiob->scbNumber;
#endif /* (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION) */

   /* Increment available count */
   SCSI_rINCREMENT_NON_EST_SCBS_AVAIL(hhcb);

}

/*********************************************************************
*
*  SCSIrDeviceQueueAppend
*
*     Append hiob into device queue
*
*  Return Value:  none
*                  
*  Parameters:    target control
*                 hiob
*                 
*  Remarks:
*                  
*********************************************************************/
void SCSIrDeviceQueueAppend (SCSI_TARGET_CONTROL SCSI_HPTR targetControl,
                             SCSI_HIOB SCSI_IPTR hiob)
{
#if SCSI_NEGOTIATION_PER_IOB
   /* If the hiob has any of the 'forceNego' flags set, increment   */
   /* the pendingNego field to indicate the one more negotiation is */
   /* needed. */
   if (hiob->SCSI_IF_forceSync || hiob->SCSI_IF_forceAsync ||
       hiob->SCSI_IF_forceWide || hiob->SCSI_IF_forceNarrow)
   {
      targetControl->pendingNego++;
   }
#endif /* SCSI_NEGOTIATION_PER_IOB */

   hiob->queueNext = SCSI_NULL_HIOB;

   if (targetControl->queueHead == SCSI_NULL_HIOB)
   {
      /* queue empty, start queue */
      targetControl->queueHead = targetControl->queueTail = hiob;
   }
   else
   {
      /* append at end */
      targetControl->queueTail->queueNext = hiob;
      targetControl->queueTail = hiob;
   }

   targetControl->deviceQueueHiobs++;
}

/*********************************************************************
*
*  SCSIrDeviceQueueRemove
*
*     Remove hiob from device queue
*
*  Return Value:  hiob removed from device queue head
*                  
*  Parameters:    target unit control
*                 
*  Remarks:       This routine assumes that there is at least one
*                 hiob in device queue
*                  
*********************************************************************/
SCSI_HIOB SCSI_IPTR SCSIrDeviceQueueRemove (SCSI_TARGET_CONTROL SCSI_HPTR targetControl)
{
   SCSI_HIOB SCSI_IPTR hiob = targetControl->queueHead;
   SCSI_UINT16 maxScbs;

#if (SCSI_NEGOTIATION_PER_IOB || SCSI_PACKETIZED_IO_SUPPORT)  
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit;
   SCSI_HHCB SCSI_HPTR hhcb;
#endif /* (SCSI_NEGOTIATION_PER_IOB || SCSI_PACKETIZED_IO_SUPPORT) */

   if (hiob != SCSI_NULL_HIOB)
   {
#if (SCSI_NEGOTIATION_PER_IOB || SCSI_PACKETIZED_IO_SUPPORT)  
      targetUnit = SCSI_TARGET_UNIT(hiob);   
      hhcb = targetUnit->hhcb;   
#endif /* (SCSI_NEGOTIATION_PER_IOB || SCSI_PACKETIZED_IO_SUPPORT) */

      /* Get the criteria factor */
      maxScbs = ((hiob->SCSI_IF_tagEnable) ? targetControl->maxTagScbs :
                                             targetControl->maxNonTagScbs);
#if SCSI_PACKETIZED_IO_SUPPORT                                          
      /* Check for auto-sense and tagEnable and packetized switching progress flag */
      /* are on; set to switch to packetiized */ 
      if (hiob->SCSI_IF_tagEnable && targetUnit->deviceTable->SCSI_DF_switchPkt          
#if (SCSI_TASK_SWITCH_SUPPORT == 1)          
          && hiob->SCSI_IF_autoSense
#endif /* SCSI_TASK_SWITCH_SUPPORT == 1 */         
          )
      {
         if (!SCSIrSwitchToPacketized(targetUnit))
         {
            maxScbs = 0;
         }                                  
      }
      /* check for target in packetized mode, and auto-sense is disable */
      /* set flag to switch to non-packetized mode */ 
      if (SCSI_hISPACKETIZED(targetUnit->deviceTable) && 
          (!hiob->SCSI_IF_tagEnable          
#if (SCSI_TASK_SWITCH_SUPPORT == 1)                    
          || !hiob->SCSI_IF_autoSense 
#endif /* SCSI_TASK_SWITCH_SUPPORT == 1 */          
          )) 
      {   
         if (!SCSIrSwitchToNonPacketized(targetUnit))
         {
            maxScbs = 0;
         }                                   
      }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

      /* If criteria is not met, simply return NULL_HIOB */
      if (maxScbs <= (targetControl->activeScbs - targetControl->deviceQueueHiobs))
      {
         hiob = SCSI_NULL_HIOB;
      }
      else
      {
#if SCSI_NEGOTIATION_PER_IOB
         
         /* If HIOB sets the flags to force negotiation, try to do the   */
         /* negotiation */
         if (hiob->SCSI_IF_forceSync || hiob->SCSI_IF_forceAsync ||
             hiob->SCSI_IF_forceWide || hiob->SCSI_IF_forceNarrow)
         {
            /* If the device is not idle, do not take the hiob out of the   */
            /* device queue as more commands are pending with the device,   */
            /* else change the xfer option parameters to do the negotiation */
            /* on the next command. */
            if (!SCSIHCheckDeviceIdle(hhcb, targetUnit->scsiID))
            {
               hiob = SCSI_NULL_HIOB;
            }
            else
            {
               SCSI_hCHANGEXFEROPTION(hiob);
               targetControl->pendingNego--;
            }
         }

         /* Remove the hiob from the device queue only if allowed to */
         if (hiob != SCSI_NULL_HIOB)
         {
            if (targetControl->queueHead == targetControl->queueTail)
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
         if (targetControl->queueHead == targetControl->queueTail)
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
#endif /* SCSI_NEGOTIATION_PER_IOB */
      }
   }

   return(hiob);
}

/*********************************************************************
*
*  SCSIrAbortHIOBInDevQ
*
*     Abort the aborting HIOB in Device queue if it found
*
*  Return Value:  0 - aborting HIOB found
*                 1 - aborting HIOB not found
*                  
*  Parameters:    aborting hiob
*                 host adapter status
*                 
*  Remarks:
*                  
*********************************************************************/
SCSI_UEXACT8 SCSIrAbortHIOBInDevQ (SCSI_HIOB SCSI_IPTR hiob,
                                   SCSI_UEXACT8 haStat)
{
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl;
   SCSI_HIOB SCSI_IPTR hiobCurr;
   SCSI_HIOB SCSI_IPTR hiobPrev;
   SCSI_UEXACT8 retStat = 0;

   targetControl = SCSI_TARGET_UNIT(hiob)->targetControl;
   hiobCurr = hiobPrev = targetControl->queueHead;

   while (hiobCurr != SCSI_NULL_HIOB)
   {
      /* Abort current HIOB if it matches */
      if (hiobCurr == hiob)
      {
#if SCSI_NEGOTIATION_PER_IOB
         /* If the hiob has any of the 'forceNego' flags set, decrement   */
         /* the pendingNego field to indicate the one less negotiation is */
         /* needed. */
         if (hiob->SCSI_IF_forceSync || hiob->SCSI_IF_forceAsync ||
             hiob->SCSI_IF_forceWide || hiob->SCSI_IF_forceNarrow)
         {
            targetControl->pendingNego--;
         }
#endif /* SCSI_NEGOTIATION_PER_IOB */
         /* Mending Device queue */
         SCSIrAdjustQueue(&targetControl->queueHead, &targetControl->queueTail,
                          &hiobPrev, hiobCurr);

         /* Post aborted HIOB */
         SCSIrPostAbortedHIOB(hiobCurr, haStat);

         targetControl->deviceQueueHiobs--;

         retStat = 1;
         break;
      }
      else
      {
         /* Aborting HIOB have not found yet, advance to the next HIOB */
         hiobPrev = hiobCurr;
         hiobCurr = hiobCurr->queueNext;
      }
   }  /* end of while loop */

   return(retStat);
}

/*********************************************************************
*
*  SCSIrClearDeviceQueue
*
*     Clear any non-active HIOB(s) in particular target's Device queue.
*
*  Return Value:  none
*                  
*  Parameters:    target unit control
*                 host adapter status
*                 
*  Remarks:       This routine will remove any HIOB(s) in a target's
*                 Device queue and post aborted HIOB back to OSM.
*                  
*********************************************************************/
void SCSIrClearDeviceQueue (SCSI_TARGET_CONTROL SCSI_HPTR targetControl,
                            SCSI_UEXACT8 haStatus)
{
   SCSI_HIOB SCSI_IPTR hiob = targetControl->queueHead;
   SCSI_HIOB SCSI_IPTR qTail = targetControl->queueTail;

   /* If the device queue is not empty */
   if (hiob != SCSI_NULL_HIOB)
   {
      while (hiob != qTail)
      {
         /* Due to the HIOB got post back to OSM immediately, we need to */
         /* save the next HIOB.  Otherwise, the queue will be broken. */
         targetControl->queueHead = targetControl->queueHead->queueNext;

#if SCSI_NEGOTIATION_PER_IOB
         /* If the hiob has any of the 'forceNego' flags set, decrement   */
         /* the pendingNego field to indicate the one less negotiation is */
         /* needed. */
         if (hiob->SCSI_IF_forceSync || hiob->SCSI_IF_forceAsync ||
             hiob->SCSI_IF_forceWide || hiob->SCSI_IF_forceNarrow)
         {
            targetControl->pendingNego--;
         }
#endif /* SCSI_NEGOTIATION_PER_IOB */
         /* Post aborted HIOB */
         SCSIrPostAbortedHIOB(hiob, haStatus);

         targetControl->deviceQueueHiobs--;

         /* Next HIOB in queue */
         hiob = targetControl->queueHead;
      }

      /* Advance to past the last HIOB in the device queue */
      targetControl->queueHead = targetControl->queueHead->queueNext;
      
#if SCSI_NEGOTIATION_PER_IOB
      /* If the hiob has any of the 'forceNego' flags set, decrement   */
      /* the pendingNego field to indicate the one less negotiation is */
      /* needed. */
      if (hiob->SCSI_IF_forceSync || hiob->SCSI_IF_forceAsync ||
          hiob->SCSI_IF_forceWide || hiob->SCSI_IF_forceNarrow)
      {
         targetControl->pendingNego--;
      }
#endif /* SCSI_NEGOTIATION_PER_IOB */
      /* Post the last aborted HIOB */
      SCSIrPostAbortedHIOB(hiob, haStatus);

      targetControl->deviceQueueHiobs--;

      /* If the queue head is null, then update the queue tail as well */
      if (targetControl->queueHead == SCSI_NULL_HIOB)
      {
         targetControl->queueTail = SCSI_NULL_HIOB;
      }
   }
}

/*********************************************************************
*
*  SCSIrClearAllDeviceQueues
*
*     Clear any non-active HIOB(s) in all Device queues.
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 host adapter status
*                 
*  Remarks:
*                  
*********************************************************************/
void SCSIrClearAllDeviceQueues (SCSI_HHCB SCSI_HPTR hhcb,
                                SCSI_UEXACT8 haStatus)
{
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl;
   SCSI_INT i;
   SCSI_INT maxDev = SCSI_MAXDEV;

   /* Search through all Device queues for any existing HIOB(s) */
   for (i = 0, targetControl = hhcb->SCSI_HP_targetControl; i < maxDev;
        i++, targetControl++)
   {
      /* Clear any HIOB(s) in a device queue */
      SCSIrClearDeviceQueue(targetControl, haStatus);
   }
}

/*********************************************************************
*
*  SCSIrHostQueueAppend
*
*     Append to host queue
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob to be appended
*                 
*  Remarks:
*                  
*********************************************************************/
void SCSIrHostQueueAppend (SCSI_HHCB SCSI_HPTR hhcb,
                           SCSI_HIOB SCSI_IPTR hiob)
{
   hiob->queueNext = SCSI_NULL_HIOB;

   if (hhcb->SCSI_WAIT_HEAD == SCSI_NULL_HIOB)
   {
      /* queue was empty */
      hhcb->SCSI_WAIT_HEAD = hhcb->SCSI_WAIT_TAIL = hiob;
   }
   else
   {
      /* append at end */
      hhcb->SCSI_WAIT_TAIL->queueNext = hiob;
      hhcb->SCSI_WAIT_TAIL = hiob;
   }
}

/*********************************************************************
*
*  SCSIrHostQueueRemove
*
*     Remove from host queue
*
*  Return Value:  hiob removed from host queue
*                 scbNumber available for reuse
*                  
*  Parameters:    hhcb
*                 
*  Remarks:       This routine assumes that there is at least one
*                 hiob in host queue
*                  
*********************************************************************/
SCSI_HIOB SCSI_IPTR SCSIrHostQueueRemove (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_HIOB SCSI_IPTR hiob = hhcb->SCSI_WAIT_HEAD;

   if (hiob != SCSI_NULL_HIOB)
   {
      /* the available scb number can be used for either tagged */
      /* nontagged request. we just remove the head from queue */
      if (hiob == hhcb->SCSI_WAIT_TAIL)
      {
         /* queue is going to be empty */
         hhcb->SCSI_WAIT_HEAD = hhcb->SCSI_WAIT_TAIL = SCSI_NULL_HIOB;
      }
      else
      {
         /* queue won't be empty */
         hhcb->SCSI_WAIT_HEAD = hhcb->SCSI_WAIT_HEAD->queueNext;
      }
   }

   return(hiob);
}

/*********************************************************************
*
*  SCSIrFreezeHostQueue
*
*     Freeze the host queue if the condition met
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 freeze-bit value
*
*  Remarks:       If the queue is not frozen then hold off all new
*                 commands by setting a freeze bit indicator.
*                  
*********************************************************************/
void SCSIrFreezeHostQueue (SCSI_HHCB SCSI_HPTR hhcb,
                           SCSI_UEXACT8 freezeVal)
{
   /* Set requested freeze-bit */
   hhcb->SCSI_WAIT_FREEZEMAP |= freezeVal;
}

/*********************************************************************
*
*  SCSIrUnFreezeHostQueue
*
*     Unfreeze the host queue if the condition met.
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 freeze-bit value
*
*  Remarks:
*                  
*********************************************************************/
void SCSIrUnFreezeHostQueue (SCSI_HHCB SCSI_HPTR hhcb,
                             SCSI_UEXACT8 freezeVal)
{
   /* If the device queue is already unfreeze, do nothing. */
   /* This is to prevent an unexpected call to unfreeze the queue. */
   if (hhcb->SCSI_WAIT_FREEZEMAP)
   {
      /* Clear requested freeze-bit */
      hhcb->SCSI_WAIT_FREEZEMAP &= ~freezeVal;
   }
}

/*********************************************************************
*
*  SCSIrAbortHIOBInHostQ
*
*     Abort the aborting HIOB in Host queue if it found
*
*  Return Value:  0 - aborting HIOB found
*                 1 - aborting HIOB not found
*                  
*  Parameters:    aborting hiob
*                 host adapter status
*                 
*  Remarks:
*                  
*********************************************************************/
SCSI_UEXACT8 SCSIrAbortHIOBInHostQ (SCSI_HIOB SCSI_IPTR hiob,
                                    SCSI_UEXACT8 haStat)
{
   SCSI_HHCB SCSI_HPTR hhcb = SCSI_TARGET_UNIT(hiob)->hhcb;
   SCSI_HIOB SCSI_IPTR hiobCurr;
   SCSI_HIOB SCSI_IPTR hiobPrev;
   SCSI_UEXACT8 retStat = 0;

   hiobCurr = hiobPrev = hhcb->SCSI_WAIT_HEAD;

   while (hiobCurr != SCSI_NULL_HIOB)
   {
      /* Abort current HIOB if it matches */
      if (hiobCurr == hiob)
      {
         /* Mending Host queue */
         SCSIrAdjustQueue(&hhcb->SCSI_WAIT_HEAD, &hhcb->SCSI_WAIT_TAIL,
                          &hiobPrev, hiobCurr);

         /* Post aborted HIOB */
         SCSIrPostAbortedHIOB(hiobCurr, haStat);

         retStat = 1;
         break;
      }
      else
      {
         /* Aborting HIOB have not found yet, advance to the next HIOB */
         hiobPrev = hiobCurr;
         hiobCurr = hiobCurr->queueNext;
      }
   }  /* end of while loop */

   return(retStat);
}

/*********************************************************************
*
*  SCSIrFindAndAbortDevQ
*
*     Find and abort any HIOB(s) in Device Queue that matched scsi id
*     and LUN of the aborting HIOB.
*
*  Return Value:  none
*                  
*  Parameters:    
*                 target unit control
*                 host adapter status
*                 
*  Remarks:
*                  
*********************************************************************/
void SCSIrFindAndAbortDevQ (SCSI_UNIT_CONTROL SCSI_UPTR targetUnit,
                            SCSI_UEXACT8 haStat)
{
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl = targetUnit->targetControl;
   SCSI_HIOB SCSI_IPTR hiob = targetControl->queueHead;
   SCSI_HIOB SCSI_IPTR qTail = targetControl->queueTail;
   SCSI_HIOB SCSI_IPTR qNext;
   /* If the device queue is not empty */
   if (hiob != SCSI_NULL_HIOB)
   {
      for(;;)   
      {
         /* Due to the HIOB got post back to OSM immediately, we need to */
         /* save the next HIOB.  Otherwise, the queue will be broken. */
         qNext = hiob->queueNext;
         if (SCSI_TARGET_UNIT(hiob)->lunID == targetUnit->lunID)
         {   
            /*Function call to reduce code redundantcy*/
            SCSIrAbortHIOBInDevQ(hiob, haStat);
         }
         if (hiob==qTail)
         {
            break;
         }
         hiob=qNext;
      }  
   }   
}

/*********************************************************************
*
*  SCSIrFindAndAbortHIOB
*
*     Find and abort any HIOB(s) in Host queue that matched scsi id
*     of the aborting HIOB.
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 target unit control
*                 host adapter status
*                 
*  Remarks:
*                  
*********************************************************************/
void SCSIrFindAndAbortHIOB (SCSI_HHCB SCSI_HPTR hhcb,
                            SCSI_UNIT_CONTROL SCSI_UPTR targetUnit,
                            SCSI_UEXACT8 haStat)
{
   SCSI_HIOB SCSI_IPTR hiobCurr;
   SCSI_HIOB SCSI_IPTR hiobPrev;
   SCSI_HIOB SCSI_IPTR qNext;
   SCSI_HIOB SCSI_IPTR qTail = hhcb->SCSI_WAIT_TAIL;

   hiobCurr = hiobPrev = hhcb->SCSI_WAIT_HEAD;

   /* If the host queue is not empty */
   if (hiobCurr != SCSI_NULL_HIOB)
   {
      while (hiobCurr != qTail)
      {
         /* Due to the HIOB got post back to OSM immediately, we need to */
         /* save the next HIOB.  Otherwise, the queue will be broken. */
         qNext = hiobCurr->queueNext;

         /* We'll abort any HIOB that has a SCSI id matching the new HIOB */
         if (SCSI_TARGET_UNIT(hiobCurr)->scsiID == targetUnit->scsiID)
         {
            if (haStat==SCSI_HOST_ABT_LUN_RST)
            {
                if (SCSI_TARGET_UNIT(hiobCurr)->lunID == targetUnit->lunID)
                {
                  /* Mending Host queue */
                  SCSIrAdjustQueue(&hhcb->SCSI_WAIT_HEAD, &hhcb->SCSI_WAIT_TAIL,
                                   &hiobPrev, hiobCurr);
     
                  /* Post aborted HIOB back to OSM */
                  SCSIrPostAbortedHIOB(hiobCurr, haStat);
                }
                else
                {
                  hiobPrev = hiobCurr;
                }
            }
            else
            {
               /* Mending Host queue */
               SCSIrAdjustQueue(&hhcb->SCSI_WAIT_HEAD, &hhcb->SCSI_WAIT_TAIL,
                                &hiobPrev, hiobCurr);
     
               /* Post aborted HIOB back to OSM */
               SCSIrPostAbortedHIOB(hiobCurr, haStat);
            }
         }
         else
         {
            hiobPrev = hiobCurr;
         }

         /* Next HIOB in queue */
         hiobCurr = qNext;
      }

      /* We'll abort any HIOB that has a SCSI id matching the new HIOB */
      if (SCSI_TARGET_UNIT(hiobCurr)->scsiID == targetUnit->scsiID)
      {
         if (haStat==SCSI_HOST_ABT_LUN_RST)
         {
             if (SCSI_TARGET_UNIT(hiobCurr)->lunID != targetUnit->lunID)
             {
               return;  
             }
         }    
         
         /* Mending Host queue */
         SCSIrAdjustQueue(&hhcb->SCSI_WAIT_HEAD, &hhcb->SCSI_WAIT_TAIL,
                          &hiobPrev, hiobCurr);
     
         /* Post aborted HIOB back to OSM */
         SCSIrPostAbortedHIOB(hiobCurr, haStat);
      }
   }
}

/*********************************************************************
*
*  SCSIrClearHostQueue
*
*     Clear any non-active HIOB(s) in Host queue
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 host adapter status
*                 
*  Remarks:       This routine will remove any HIOB(s) in Host queue
*                 and put aborted HIOB into Done queue.
*                  
*********************************************************************/
void SCSIrClearHostQueue (SCSI_HHCB SCSI_HPTR hhcb,
                          SCSI_UEXACT8 haStatus)
{
   SCSI_HIOB SCSI_IPTR hiob = hhcb->SCSI_WAIT_HEAD;
   SCSI_HIOB SCSI_IPTR qTail = hhcb->SCSI_WAIT_TAIL;

   /* If the host queue is not empty */
   if (hiob != SCSI_NULL_HIOB)
   {
      while (hiob != qTail)
      {
         /* Due to the HIOB got post back to OSM immediately, we need to */
         /* save the next HIOB.  Otherwise, the queue will be broken. */
         hhcb->SCSI_WAIT_HEAD = hhcb->SCSI_WAIT_HEAD->queueNext;

         /* Post aborted HIOB back to OSM */
         SCSIrPostAbortedHIOB(hiob, haStatus);

         /* Next HIOB in queue */
         hiob = hhcb->SCSI_WAIT_HEAD;
      }

      /* Advance to past the last HIOB in the host queue */
      hhcb->SCSI_WAIT_HEAD = hhcb->SCSI_WAIT_HEAD->queueNext;

      /* Post the last aborted HIOB back to OSM */
      SCSIrPostAbortedHIOB(hiob, haStatus);

      /* If the queue head is null, then update the queue tail as well */
      if (hhcb->SCSI_WAIT_HEAD == SCSI_NULL_HIOB)
      {
         hhcb->SCSI_WAIT_TAIL = SCSI_NULL_HIOB;
      }
   }
}

/*********************************************************************
*
*  SCSIrFreeScb
*
*     Free scb associated
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       This routine will free scb specified. if there was
*                 any hiob queued for waiting scb available then this
*                 new available scb will be used to continue queued hiob
*                 processing.
*                  
*********************************************************************/
void SCSIrFreeScb (SCSI_HHCB SCSI_HPTR hhcb,
                   SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_HIOB SCSI_IPTR hiobNew;

   /* First return SCB to free pool */
   SCSI_rRETURNSCB(hhcb,hiob);
   
   if ((!hhcb->SCSI_WAIT_FREEZEMAP) &&
       (hhcb->SCSI_WAIT_HEAD != SCSI_NULL_HIOB))
   {
      /* Host queue not frozen and there is an hiob to execute. */
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

   /* continue to free criteria */
   SCSI_rFREECRITERIA(hiob);
}

/*********************************************************************
*
*  SCSIrFreeCriteria
*
*     Free criteria associated specified
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:       This routine will free scb specified. if there was
*                 any hiob queued for waiting scb available then this
*                 new available scb will be used to continue queued hiob
*                 processing.
*                  
*********************************************************************/
void SCSIrFreeCriteria (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_HHCB SCSI_HPTR hhcb = targetUnit->hhcb;
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl = targetUnit->targetControl;
   SCSI_HIOB SCSI_IPTR hiobNew;
   SCSI_UINT16 maxScbs;

   /* get checking criteria */
   maxScbs = ((hiob->SCSI_IF_tagEnable) ? targetControl->maxTagScbs :
                                 targetControl->maxNonTagScbs);

   if (!targetControl->freezeMap)
   {
#if SCSI_NEGOTIATION_PER_IOB
      /* Some hiobs are left in the device queue if there are more scbs     */
      /* waiting in the rsm layer than maximum allowed tags, OR there is    */
      /* negotiation pending; so just check to see if any HIOBs are sitting */
      /* in the device queue. */
      targetControl->activeScbs--;
      while (targetControl->deviceQueueHiobs)
#else /* SCSI_NEGOTIATION_PER_IOB */
      if (--targetControl->activeScbs >= maxScbs)
#endif /* SCSI_NEGOTIATION_PER_IOB */
      {
         /* some one must be waiting for criteria free */
         if ((hiobNew = SCSIrDeviceQueueRemove(targetControl)) !=
             SCSI_NULL_HIOB)
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

      else if (targetControl->freezeMap & SCSI_FREEZE_ERRORBUSY)
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
            /* Unfreeze the device queue and dispatch any queued HIOBs */
            /* if the queue is not frozen. */
            SCSIrUnFreezeDeviceQueue(SCSI_TARGET_UNIT(hiob)->targetControl,
                                     SCSI_FREEZE_HIPRIEXISTS);
            SCSIrDispatchQueuedHIOB(hiob);
         }
      }
   }
}

/*********************************************************************
*
*  SCSIrPostAbortedHIOB
*
*     Setup abort status and post back aborted HIOB to OSM
*
*  Return Value:  none
*                  
*  Parameters:    hiob to be aborted
*                 host adapter status
*                 
*  Remarks:
*                  
*********************************************************************/
void SCSIrPostAbortedHIOB (SCSI_HIOB SCSI_IPTR hiob,
                           SCSI_UEXACT8 haStatus)
{
#if SCSI_TARGET_OPERATION 
   if (!hiob->SCSI_IP_targetMode)
      /* if not a target mode HIOB */
#endif /* SCSI_TARGET_OPERATION */   
      /* Decrement number of active Scbs for particular target */
      --(SCSI_TARGET_UNIT(hiob)->targetControl->activeScbs);

   /* Set up abort status */
   hiob->stat = SCSI_SCB_ABORTED;
   hiob->haStat = haStatus;

   /* Post back aborted HIOB to OSM */
   OSD_COMPLETE_HIOB(hiob);
}

/*********************************************************************
*
*  SCSIrPostDoneQueue
*
*     Posting all hiobs in done queue
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:
*
*********************************************************************/
void SCSIrPostDoneQueue (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_HIOB SCSI_IPTR hiob;
   
   /* Post all HIOB(s) in DoneQ */
   while (hhcb->SCSI_DONE_HEAD != SCSI_NULL_HIOB)
   {
      hiob = hhcb->SCSI_DONE_HEAD;
      hhcb->SCSI_DONE_HEAD=hiob->queueNext;

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
      OSD_COMPLETE_HIOB(hiob);
   }
   /* All DoneQ element have been posted back. The Q is empty now */
   hhcb->SCSI_DONE_TAIL = SCSI_NULL_HIOB;
   
}

/*********************************************************************
*
*  SCSIrAdjustQueue
*
*     Adjust queue as the result of aborting a HIOB
*
*  Return Value:  none
*                  
*  Parameters:    pointer to queue head
*                 pointer to queue tail
*                 pointer to previous hiob
*                 current hiob
*                 
*  Remarks:
*                  
*********************************************************************/
void SCSIrAdjustQueue (SCSI_HIOB SCSI_IPTR SCSI_IPTR pqHead,
                       SCSI_HIOB SCSI_IPTR SCSI_IPTR pqTail,
                       SCSI_HIOB SCSI_IPTR SCSI_IPTR phiobPrev,
                       SCSI_HIOB SCSI_IPTR hiobCurr)
{
   /* Aborted HIOB is the only one in the queue */
   if (*pqHead == *pqTail)
   {
      /* Queue empty */
      *pqHead = *pqTail = SCSI_NULL_HIOB;
   }
   else if (hiobCurr == *pqHead)
   {
      /* Aborted HIOB is at the head of the queue */
      *pqHead = (*pqHead)->queueNext;
   }
   else
   {
      /* Aborted HIOB is either at the middle or tail of the queue */
      (*phiobPrev)->queueNext = hiobCurr->queueNext;
      if (hiobCurr == *pqTail)
      {
         /* Adjust tail */
         *pqTail = *phiobPrev;
      }
   }
}

/*********************************************************************
*
*  SCSIrFinishQueueFull
*
*     Handle Queue Full condition
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*
*  Remarks:       This routine will handle queue full status for a
*                 device.  If the device has no scbs active, then 
*                 any hiob queued for waiting scb available then this
*                 new available scb will be used to continue queued hiob
*                 processing.
*                  
*********************************************************************/
void SCSIrFinishQueueFull (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl = targetUnit->targetControl;

   /* Clear queue full condition and dispatch all queued HIOBs. */
   SCSIrUnFreezeDeviceQueue(targetControl, SCSI_FREEZE_QFULL);
   SCSIrDispatchQueuedHIOB(hiob);
}

/*********************************************************************
*
*  SCSIrFreezeAllDeviceQueues
*
*     Freeze all device queues.
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 
*  Remarks:
*                  
*********************************************************************/
void SCSIrFreezeAllDeviceQueues (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl;
   SCSI_INT i;
   SCSI_INT maxDev = SCSI_MAXDEV;

   for (i = 0, targetControl = hhcb->SCSI_HP_targetControl; i < maxDev;
        i++, targetControl++)
   {
      /* Freeze this device queue */
      SCSIrFreezeDeviceQueue(targetControl, SCSI_FREEZE_EVENT);
   }
}

/*********************************************************************
*
*  SCSIrUnFreezeAllDeviceQueues
*
*     UnFreeze all device queues.
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 freeze-bit value
*                 
*  Remarks:
*                  
*********************************************************************/
void SCSIrUnFreezeAllDeviceQueues (SCSI_HHCB SCSI_HPTR hhcb,
                                   SCSI_UINT8 freezeVal)
{
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl;
   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_INT i;
   SCSI_INT maxDev = SCSI_MAXDEV;

   for (i = 0, targetControl = hhcb->SCSI_HP_targetControl; i < maxDev;
        i++, targetControl++)
   {
      /* Unfreeze this device queue */
      SCSIrUnFreezeDeviceQueue(targetControl, freezeVal);
      
      /* Rule of thumb, we should dispatch any pending HIOB(s) */
      /* if any after the queue unfrozen. */
      hiob = targetControl->queueHead;
      if (hiob != SCSI_NULL_HIOB)
      {
         SCSIrDispatchQueuedHIOB(hiob);
      }
   }
}

/*********************************************************************
*
*  SCSIrFreezeDeviceQueue
*
*     Freeze the device queue if the condition met
*
*  Return Value:  none
*                  
*  Parameters:    targetControl
*                 freeze-bit value
*
*  Remarks:       If the queue is not frozen then hold off all new
*                 commands for the device by assigning maxNonTagScbs
*                 and maxTagScbs to 0.  The original values are saved
*                 in origMaxNonTagScbs and origMaxTagScbs respectively.
*                  
*********************************************************************/
void SCSIrFreezeDeviceQueue (SCSI_TARGET_CONTROL SCSI_HPTR targetControl,
                             SCSI_UINT8 freezeVal)
{
   /* If the queue is not frozen, */
   /* then set the maxNonTagScbs and maxTagScbs to zero. */
   if (!targetControl->freezeMap)
   {
      if (targetControl->maxNonTagScbs > 0)
      {
         targetControl->origMaxNonTagScbs = targetControl->maxNonTagScbs;
         targetControl->maxNonTagScbs = 0;
      }

      if (targetControl->maxTagScbs > 0)
      {
         targetControl->origMaxTagScbs = targetControl->maxTagScbs;
         targetControl->maxTagScbs = 0;
      }
   }
   
   /* Set requested freeze-bit: */
   targetControl->freezeMap |= freezeVal;
}

/*********************************************************************
*
*  SCSIrUnFreezeDeviceQueue
*
*     Unfreeze the device queue if the condition met.
*
*  Return Value:  none
*                  
*  Parameters:    targetControl
*                 freeze-bit value
*
*  Remarks:
*                  
*********************************************************************/
void SCSIrUnFreezeDeviceQueue (SCSI_TARGET_CONTROL SCSI_HPTR targetControl,
                               SCSI_UINT8 freezeVal)
{
   /* If the device queue is already unfreeze, do nothing. */
   /* This is to prevent an unexpected call to unfreeze the queue. */
   if (targetControl->freezeMap)
   {
      /* Clear requested freeze-bit */
      targetControl->freezeMap &= ~freezeVal;

      /* If the queue is not frozen, */
      if (!targetControl->freezeMap)
      {
         /* then restore the original maxNonTagScbs and maxTagScbs. */
         /* Also reset both origMaxNonTagScbs and origMaxTagScbs to 0. */
         targetControl->maxNonTagScbs = targetControl->origMaxNonTagScbs;
         targetControl->origMaxNonTagScbs = 0;
         targetControl->maxTagScbs = targetControl->origMaxTagScbs;
         targetControl->origMaxTagScbs = 0;
      }
   }
}

/*********************************************************************
*
*  SCSIrDispatchQueuedHIOB
*
*     Dispatch any queued HIOB(s) that meet the criteria
*     after the device queue unfroze.
*
*  Return Value:  none
*                  
*  Parameters:    hiob
*                 freeze-bit value
*
*  Remarks:
*                  
*********************************************************************/
void SCSIrDispatchQueuedHIOB (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl = targetUnit->targetControl;
   SCSI_HIOB SCSI_IPTR hiobNew;
   SCSI_UINT16 maxScbs;
   SCSI_UINT16 cnt = 0;

   /* Dispatch if the queue is not frozen, */
   if ((!targetControl->freezeMap) &&
         ((hiobNew = SCSIrDeviceQueueRemove(targetControl)) != SCSI_NULL_HIOB))
   {
      /* Get the criteria factor */
      maxScbs = ((hiobNew->SCSI_IF_tagEnable) ? targetControl->maxTagScbs :
                                                targetControl->maxNonTagScbs);
      SCSIrAllocateScb(targetUnit->hhcb,hiobNew);
      maxScbs--;

      /* Walk down device queue and execute until new criteria is reached     */
      /* or device queue is empty.  To enter this function, the all active    */
      /* cmds for the device must have been completed.  Therefore, the cnt    */
      /* in the loop below can start at zero. Also, there is no need to       */
      /* adjust targetControl->activeScbs since it was already updated when   */
      /* the scb was put into the device queue in the first place.            */
      while(1)
      {
         if (++cnt > maxScbs)
         {
            break;
         }
         if ((hiobNew = SCSIrDeviceQueueRemove(targetControl)) != SCSI_NULL_HIOB)
         {
            SCSIrAllocateScb(targetUnit->hhcb,hiobNew);
         }
         else
         {
            break;
         }
      }
   }
}

/*********************************************************************
*
*  SCSIrAbortTargetHIOB
*
*     Setup abort status and call OSD_COMPLETE_SPECIAL_HIOB
*     for Target mode HIOB 
*
*  Return Value:  none
*                  
*  Parameters:    hiob to be aborted
*                 host adapter status
*                 
*  Remarks:
*                  
*********************************************************************/
#if SCSI_TARGET_OPERATION 
void SCSIrAbortTargetHIOB (SCSI_HIOB SCSI_IPTR hiob,
                           SCSI_UEXACT8 haStatus)
{
   SCSI_HHCB SCSI_HPTR hhcb = SCSI_NEXUS_UNIT(hiob)->hhcb;
   
   SCSI_NEXUS_UNIT(hiob)->SCSI_XP_hiobActive = 0;

   /* Set up abort status */
   hiob->stat = SCSI_SCB_ABORTED;
   hiob->haStat = haStatus;
   
   /* Complete HIOB */
   OSD_COMPLETE_SPECIAL_HIOB(hiob);
}
#endif /* SCSI_TARGET_OPERATION */ 

/*********************************************************************
*
*  SCSIrFindAndAbortTargetHIOBs
*
*     Find and abort Target Mode HIOB(s) in Host queue that match the
*     conditions specified by the haStat parameters. 
*
*  Return Value:  the number of HIOBs aborted
*                  
*  Parameters:    hhcb
*                 nexus
*                 haStat - contains abort reason 
*                                 
*  Remarks:
*                  
*********************************************************************/
#if SCSI_TARGET_OPERATION 
SCSI_UEXACT8 SCSIrFindAndAbortTargetHIOBs (SCSI_HHCB SCSI_HPTR hhcb,
                                           SCSI_NEXUS  SCSI_XPTR nexus,
                                           SCSI_UEXACT8 haStat)
{
   SCSI_HIOB SCSI_IPTR hiobCurr;
   SCSI_HIOB SCSI_IPTR hiobPrev;  
   SCSI_HIOB SCSI_IPTR hiobNext;
   SCSI_BOOLEAN conditionMet = 0;
   SCSI_UEXACT8 numAborted = 0;

   hiobCurr = hiobPrev = hhcb->SCSI_WAIT_HEAD;

   while (hiobCurr != SCSI_NULL_HIOB)
   {
      if (hiobCurr->SCSI_IP_targetMode)
      {
         /* Only consider Target Mode IOBs in the queue */

         switch (haStat)
         {
            case SCSI_HOST_TARGET_RESET:
               /* SCSI-2 Bus device reset or SCSI-3 Target Reset */
               /* abort all target mode IOBs */
               conditionMet = 1; 
               break;

            case SCSI_HOST_ABORT_TASK_SET:
               /* SCSI-2 Abort or SCSI-3 Abort Task Set */
            case SCSI_HOST_LUN_RESET:
               /* SCSI-3 Logical Unit Reset */

               /* Abort IOBs identified by SCSI ID and Lun */ 
               if ((SCSI_NEXUS_UNIT(hiobCurr)->scsiID == nexus->scsiID) &&
#if SCSI_MULTIPLEID_SUPPORT
                   (SCSI_NEXUS_UNIT(hiobCurr)->selectedID == nexus->selectedID) &&
#endif /* SCSI_MULTIPLEID_SUPPORT */
                  (SCSI_NEXUS_UNIT(hiobCurr)->lunID == nexus->lunID))
               {
                  conditionMet = 1; 
               }
               break;

            case SCSI_HOST_ABORT_TASK:
               /* SCSI-2 Abort Tag or SCSI-3 AbortTask */
               /* Aborts IOBs identified by SCSI ID, Lun, and Tag */
               if ((SCSI_NEXUS_UNIT(hiobCurr)->scsiID == nexus->scsiID) &&
#if SCSI_MULTIPLEID_SUPPORT
                   (SCSI_NEXUS_UNIT(hiobCurr)->selectedID == nexus->selectedID) &&
#endif /* SCSI_MULTIPLEID_SUPPORT */
                   (SCSI_NEXUS_UNIT(hiobCurr)->lunID == nexus->lunID) &&
                   (SCSI_NEXUS_UNIT(hiobCurr)->queueTag = nexus->queueTag))
               {
                  conditionMet = 1; 
               }
               break;
         
            case SCSI_HOST_CLEAR_TASK_SET:
               /* SCSI-2 Clear Queue or SCSI-3 ClearTask Set */
               /* Abort IOBs identified by lun */ 
               if (SCSI_NEXUS_UNIT(hiobCurr)->lunID == nexus->lunID)
               {
                  conditionMet = 1;
               }
               break;

            case SCSI_HOST_TERMINATE_TASK:
               /* SCSI-2 Terminate I/O process or SCSI-3 Terminate Task */
               /* Do nothing for now */
               break;

            case SCSI_HOST_ABT_HOST:
               /* Abort due to a SCSI_ABORT_NEXUS  */
               if (SCSI_NEXUS_UNIT(hiobCurr) == nexus)
               {
                  conditionMet = 1;
               }
               break;
                   
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
            case SCSI_HOST_IU_STATE_CHANGE:
               /* IU_REQ change - abort any I/Os queued for this SCSI ID */
               if ((SCSI_NEXUS_UNIT(hiobCurr)->scsiID == nexus->scsiID)
#if SCSI_MULTIPLEID_SUPPORT
                      &&
                   (SCSI_NEXUS_UNIT(hiobCurr)->selectedID == nexus->selectedID)
#endif /* SCSI_MULTIPLEID_SUPPORT */
                  )
               {
                  conditionMet = 1;
               }
               break;
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */

            default:
               /* should never get here */
               break;

         } 
      }
      
      /* save the next HIOB as hiobCurr may be posted back to OSM */
      hiobNext = hiobCurr->queueNext; 
                
      if (conditionMet)
      {
                  
         /* Mending Host queue */
         SCSIrAdjustQueue(&hhcb->SCSI_WAIT_HEAD, &hhcb->SCSI_WAIT_TAIL,
                          &hiobPrev, hiobCurr);
                          
         /* Post aborted HIOB back to OSM */
         SCSIrAbortTargetHIOB(hiobCurr, haStat);
         numAborted++;       
         conditionMet = 0;
      }
      else
      {
         hiobPrev = hiobCurr; 
      }
      
      /* Next HIOB in queue */
      hiobCurr = hiobNext;
   }

   return(numAborted);

}
#endif /* SCSI_TARGET_OPERATION */ 

/*********************************************************************
*
*  SCSIrReturnEstScb
*
*     Return est scb to free pool. If there are resources available
*    (HIOB and NEXUS) then requeue to HWM layer. 
*     
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob containing scbNumber to be returned
*
*  Remarks:
*                  
*********************************************************************/
#if SCSI_TARGET_OPERATION 
void SCSIrReturnEstScb (SCSI_HHCB SCSI_HPTR hhcb,
                        SCSI_HIOB SCSI_IPTR hiob)
{

   SCSIrReturnScb(hhcb,hiob);

   /* Decrement non Establish SCB available count as it was 
    * incremented in SCSIrReturnScb.
    */
   hhcb->SCSI_NON_EST_SCBS_AVAIL--;

   /* Increment the Establish SCB available count. */
   hhcb->SCSI_EST_SCBS_AVAIL++;

}
#endif /* SCSI_TARGET_OPERATION */

/*********************************************************************
*
*  SCSIrGetEstScb
*
*     Get free establish connection scb if available
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
#if SCSI_TARGET_OPERATION 
SCSI_UEXACT16 SCSIrGetEstScb (SCSI_HHCB SCSI_HPTR hhcb,
                              SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_UEXACT16 scbNumber;

   if (hhcb->SCSI_EST_SCBS_AVAIL)
   {
      /* Simply call regular get scb routine. */
      scbNumber = SCSIrGetScb(hhcb,hiob);
   
      if (!SCSI_IS_NULL_SCB(scbNumber))
      {
         /* Increment non est scb available count as it was 
          * decremented in SCSIrReturnScb.
          */
         hhcb->SCSI_NON_EST_SCBS_AVAIL++;

         /* Decrement est scb available count. */
         hhcb->SCSI_EST_SCBS_AVAIL--;
      } 
   }
   else
   {
      /* No est scbs available */
      SCSI_SET_NULL_SCB(scbNumber);
   }

   return(scbNumber);

}
#endif /* SCSI_TARGET_OPERATION */

/*********************************************************************
*
*  SCSIrQueueEstHiobs
*
*     Queue establish Connection HIOBs from HIOB queue
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
#if SCSI_TARGET_OPERATION 
void SCSIrQueueEstHiobs (SCSI_HHCB SCSI_HPTR hhcb)
{

   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_NEXUS SCSI_XPTR nexus;

   while ((hiob = SCSIrHiobPoolRemove(hhcb)) != SCSI_NULL_HIOB)
   {
      if ((nexus = SCSIrAllocateNexus(hhcb)) == SCSI_NULL_NEXUS)
      {
         SCSIrQueueEstHiobPool(hhcb,hiob);
         break;
      }
      else
      {
         if (!SCSI_IS_NULL_SCB(hiob->scbNumber = SCSI_rGETESTSCB(hhcb,hiob)))
         {
            SCSI_NEXUS_UNIT(hiob) = nexus;
            hiob->queueNext = SCSI_NULL_HIOB;
            SCSIHQueueSpecialHIOB(hiob);
         }  
         else
         {
            /* free the nexus */
            SCSIrFreeNexus(hhcb,nexus);
            /* Queue to Est HIOB queue */
            SCSIrQueueEstHiobPool(hhcb,hiob);  
            break;
         }
      }    
   }      

}
#endif /* SCSI_TARGET_OPERATION */ 

/*********************************************************************
*
*  SCSIrAbortHIOBInHiobQ
*
*     Abort the aborting HIOB in HIOB Pool queue if it found
*
*  Return Value:  0 - aborting HIOB not found
*                 1 - aborting HIOB found
*                  
*  Parameters:    aborting hiob
*                 host adapter status
*                 
*  Remarks:
*                 This routine is only used in Target mode.
*             
*********************************************************************/
#if SCSI_TARGET_OPERATION 
SCSI_UEXACT8 SCSIrAbortHIOBInHiobQ (SCSI_HIOB SCSI_IPTR hiob,
                                    SCSI_UEXACT8 haStat)
{
   SCSI_UEXACT8 retStat = 0;
   SCSI_HHCB SCSI_HPTR hhcb;
   SCSI_HIOB SCSI_IPTR hiobCurr;
   SCSI_HIOB SCSI_IPTR hiobPrev;
   SCSI_UEXACT16 queueCnt;

   /* Determine if related HIOB is target mode Establish 
    * Connection HIOB that has not been queued to the HWM layer.
    * If not, then don't search this queue.
    */
   if (hiob->SCSI_IP_targetMode &&
       hiob->cmd == SCSI_CMD_ESTABLISH_CONNECTION &&
       (!hiob->SCSI_IP_estQueuedToHwm)) 
   {
      hhcb = SCSI_INITIATOR_UNIT(hiob);
      /* if there are no HIOBs in queue then return. */
      if (hhcb->SCSI_HF_targetHiobQueueCnt == 0)
      {
         return(retStat);
      }
   }
   else
   {
      return(retStat);
   }

   hiobCurr = hiobPrev = hhcb->SCSI_HF_targetHiobQueue;

   while (hiobCurr != SCSI_NULL_HIOB)
   {
      /* Abort current HIOB if it matches */
      if (hiobCurr == hiob)
      {
         if (hiobCurr == hiobPrev)
         {
            /* Aborted HIOB is at the head of queue */
            hhcb->SCSI_HF_targetHiobQueue = hiobCurr->queueNext;
         }
         else
         {
            /* Aborted HIOB is in the middle or tail of the queue */ 
            hiobPrev->queueNext = hiobCurr->queueNext;
         }
         queueCnt = hhcb->SCSI_HF_targetHiobQueueCnt;
         queueCnt--;
         hhcb->SCSI_HF_targetHiobQueueCnt = queueCnt;
         
         /* Check threshold */
         if (hhcb->SCSI_HF_targetHiobQueueThreshold)
         {
            /* Check if OSMEvent to be sent */   
            if (queueCnt == hhcb->SCSI_HF_targetHiobQueueThreshold)
            {
               /* send a Alert to OSM */
               SCSIrAsyncEvent(hhcb,SCSI_AE_EC_IOB_THRESHOLD);
            }
         } 
         
         /* Complete HIOB */
         hiobCurr->stat = SCSI_SCB_ABORTED;
         hiobCurr->haStat = haStat;
         OSD_COMPLETE_SPECIAL_HIOB(hiobCurr);                  
         retStat = 1;
         break;
      }
      else
      {
         /* Aborting HIOB have not found yet, advance to the next HIOB */
         hiobPrev = hiobCurr;
         hiobCurr = hiobCurr->queueNext;
      }
   }  /* end of while loop */

   return(retStat);
}
#endif /* SCSI_TARGET_OPERATION */

/*********************************************************************
*
*  SCSIrFreeBusHeldScb
*
*     Return SCB used in target mode bus held situation to the
*     appropriate SCB pool.  
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 
*  Remarks:
*                 This routine is only used in Target mode.
*             
*********************************************************************/
#if SCSI_TARGET_OPERATION 
void SCSIrFreeBusHeldScb (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_HIOB fakeHiob;
   SCSI_HIOB SCSI_IPTR hiob;

   /* Create a fake hiob with the relevant fields assigned
    * to free SCB.
    */ 
   hiob = &fakeHiob;

   OSDmemset(hiob,0,sizeof(SCSI_HIOB));
   hiob->scbNumber = hhcb->SCSI_HP_busHeldScbNumber;
   hiob->SCSI_IP_targetMode = 1;

   /* indicator for where to return scb */
   if (hhcb->SCSI_HP_estScb)
   {
      /* return est scb to available pool */
      SCSI_rRETURNESTSCB(hhcb,hiob);
   }
   else
   {
      /* return to regular SCB pool */
      SCSIrFreeScb(hhcb,hiob);
   }
}
#endif /* SCSI_TARGET_OPERATION */

/*********************************************************************
*
*  SCSIrTargetFindAndSetNexusIuReqChange
*
*     Searches nexus memory for nexus that match the scsiID
*     (and selectedID for multiple target id support) of
*     the nexus parameter. When found the iuReqChange indicator
*     is set in the nexus.
*
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 nexus
*
*  Remarks:
*                       
*                  
*********************************************************************/
#if (SCSI_TARGET_OPERATION && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
void SCSIrTargetFindAndSetNexusIuReqChange (SCSI_HHCB SCSI_HPTR hhcb,
                                            SCSI_NEXUS SCSI_XPTR nexus)
{
   SCSI_NEXUS SCSI_XPTR nextNexus;
   SCSI_UEXACT16 nexusHandleCnt = hhcb->SCSI_HF_targetNumberNexusHandles; 
   SCSI_UEXACT16 i;

   OSDDebugPrint(8,("\nEntered SCSIhTargetFindAndSetNexusIuReqChange "));
   
   if (nexusHandleCnt != 0)
   {
      for (i = 0, nextNexus = hhcb->SCSI_HF_targetNexusMemory; i < nexusHandleCnt;
           i++, nextNexus++)
      {
         if ((!nextNexus->SCSI_XF_available) &&
             (nextNexus->SCSI_XP_nexusActive))
         {
            if ((nextNexus->scsiID == nexus->scsiID)
#if SCSI_MULTIPLEID_SUPPORT
                         && 
                (nextNexus->selectedID == nexus->selectedID)
#endif /* SCSI_MULTIPLEID_SUPPORT */
               )
             {
               /* Mark nexus as iuReqChange */
               nextNexus->SCSI_XP_iuReqChange = 1;
            }
         }
      }
   }
}
#endif /* (SCSI_TARGET_OPERATION && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */

/*********************************************************************
*
*  SCSIrSwitchToPacketized
*
*     Switch from non-packetized to packetized mode
*
*  Return Value:  1 - switching
*                 0 - not switching
*                  
*  Parameters:    targetUnit
*                 
*  Remarks:
*
*********************************************************************/
#if SCSI_PACKETIZED_IO_SUPPORT
SCSI_UEXACT8 SCSIrSwitchToPacketized (SCSI_UNIT_CONTROL SCSI_UPTR targetUnit)
{
   SCSI_DEVICE SCSI_DPTR deviceTable = targetUnit->deviceTable;
   
   if (SCSIHCheckDeviceIdle(targetUnit->hhcb, targetUnit->scsiID) == SCSI_IDLE)
   {                                                                                                                         
      deviceTable->SCSI_DF_needNego = 1;

      /* turn off flag for switching progress */
      deviceTable->SCSI_DF_switchPkt = 0; 

      return (1);
   } 

   return (0);
}
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

/*********************************************************************
*
*  SCSIrSwitchToNonPacketized
*
*     Switch from packetized to Non-packetized mode
*
*  Return Value:  1 - switching
*                 0 - not switching
*                  
*  Parameters:    targetUnit
*                 
*
*  Remarks:
*
*********************************************************************/
#if SCSI_PACKETIZED_IO_SUPPORT
SCSI_UEXACT8 SCSIrSwitchToNonPacketized (SCSI_UNIT_CONTROL SCSI_UPTR targetUnit)
{
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_UEXACT8 i;
   
   if (SCSIHCheckDeviceIdle(targetUnit->hhcb, targetUnit->scsiID) == SCSI_IDLE)
   {                                                 
      deviceTable = targetUnit->deviceTable;
      deviceTable->SCSI_DF_needNego = 1 ;  

      /* Setup hardware to re-negotiate in packetized mode */
      SCSIHSetupNegoInPacketized(targetUnit->hhcb,targetUnit->scsiID);

      /* turn on flag for switching progress */
      deviceTable->SCSI_DF_switchPkt = 1;  

      /* Use temporary transfer rate entry for negotiation.  Because   */
      /* the current NEGO_XFER index might point to some transfer rate */
      /* entry that we don't want to lose the data.                    */
      for (i = 0; i < SCSI_MAX_XFER_PARAMETERS; i++)
      {
         SCSI_TEMP_XFER(deviceTable)[i] = SCSI_NEGO_XFER(deviceTable)[i];
      }

      /* Set flag to switch off QAS/Packetized mode */
      SCSI_TEMP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &=
                  (SCSI_UEXACT8) ~(SCSI_QUICKARB | SCSI_PACKETIZED);

      /* Make TEMP entry to be the current negoXferIndex */
      deviceTable->negoXferIndex = SCSI_TEMP_ENTRY;

      return (1); 
   }

   return (0); 
}
#endif /* SCSI_PACKETIZED_IO_SUPPORT */





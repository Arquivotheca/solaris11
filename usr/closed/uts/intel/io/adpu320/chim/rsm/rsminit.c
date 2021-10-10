/*$Header: /vobs/u320chim/src/chim/rsm/rsminit.c   /main/19   Sat Apr 19 14:51:34 2003   spal3094 $*/

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
*  Module Name:   RSMINIT.C
*
*  Description:
*                 Codes to initialize resource management layer
*
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         NONE
*
*  Entry Point(s):
*                 SCSIRGetConfiguration
*                 SCSIRGetMemoryTable
*                 SCSIRApplyMemoryTable
*                 SCSIRInitialize
*
***************************************************************************/

#include "scsi.h"
#include "rsm.h"

/*********************************************************************
*
*  SCSIRGetConfiguration
*
*     Get default configuration information
*
*  Return Value:  status
*                 0 - configuration available
*                  
*  Parameters:    hhcb
*
*  Remarks:       THis routine requires SCSIRegister valid in
*                 order to access the hardware. This routine
*                 may also access pci configuration space in 
*                 order to collect configuration information.
*
*********************************************************************/
int SCSIRGetConfiguration (SCSI_HHCB SCSI_HPTR hhcb)
{
   /* get configuration information filled by hardware management */
   SCSIHGetConfiguration(hhcb);

   hhcb->maxNonTagScbs = SCSI_rMAX_NONTAG_SCBS(hhcb);
   hhcb->maxTagScbs = 4;

   /* get target mode resource configuration */
   SCSI_rTARGETGETCONFIG(hhcb);

   return(0);
}

/*********************************************************************
*
*  SCSIRGetMemoryTable
*
*     This routine will collect memory requirement information and 
*     fill the memory table.
*
*  Return Value:  none
*                  
*  Parameters:    firmwareMode
*                 numberScbs
*                 numberNexusHandles
*                 numberNodeHandles
*                 memoryTable
*
*  Remarks:       This routine may get called before/after 
*                 SCSIRGetConfiguration. It requires firmwareMode set
*                 with intended operating mode before calling
*                 this routine.
*
*                 This routine should not assume harwdare can be
*                 accessed. (e.g. SCSIRegister may not be valid
*                 when this routine get called.
*                 
*********************************************************************/
void SCSIRGetMemoryTable (SCSI_UEXACT8 firmwareMode,
                          SCSI_UEXACT16 numberScbs,
                          SCSI_UEXACT16 numberNexusHandles,
                          SCSI_UEXACT8 numberNodeHandles,
                          SCSI_MEMORY_TABLE SCSI_HPTR memoryTable)
{
   /* get memory table filled by hardware management */
   SCSIHGetMemoryTable(firmwareMode,numberScbs,memoryTable);

#if (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION)
   memoryTable->memory[SCSI_HM_FREEPACKETIZEDSTACK].memoryType = SCSI_MT_HPTR;
   memoryTable->memory[SCSI_HM_FREEPACKETIZEDSTACK].memoryCategory = SCSI_MC_UNLOCKED;
   memoryTable->memory[SCSI_HM_FREEPACKETIZEDSTACK].memoryAlignment = (SCSI_UEXACT8)0x03;
   if (numberScbs > (SCSI_UEXACT16)256)
   {
      memoryTable->memory[SCSI_HM_FREEPACKETIZEDSTACK].minimumSize = 
         memoryTable->memory[SCSI_HM_FREEPACKETIZEDSTACK].memorySize =
            sizeof(SCSI_UEXACT16) * (numberScbs - (SCSI_UEXACT16)256);
   }
   else
   {
      memoryTable->memory[SCSI_HM_FREEPACKETIZEDSTACK].minimumSize = 
         memoryTable->memory[SCSI_HM_FREEPACKETIZEDSTACK].memorySize = 0;
   }
   memoryTable->memory[SCSI_HM_FREEPACKETIZEDSTACK].granularity = 0;
#endif /* (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION) */

   memoryTable->memory[SCSI_HM_FREENONPACKETIZEDSTACK].memoryType = SCSI_MT_HPTR;
   memoryTable->memory[SCSI_HM_FREENONPACKETIZEDSTACK].memoryCategory = SCSI_MC_UNLOCKED;
   memoryTable->memory[SCSI_HM_FREENONPACKETIZEDSTACK].memoryAlignment = (SCSI_UEXACT8)0x03;
   if (numberScbs > (SCSI_UEXACT16)256)
   {
      memoryTable->memory[SCSI_HM_FREENONPACKETIZEDSTACK].minimumSize = 
         memoryTable->memory[SCSI_HM_FREENONPACKETIZEDSTACK].memorySize =
            sizeof(SCSI_UEXACT16) * (SCSI_UEXACT16)256;
   }
   else
   {
      memoryTable->memory[SCSI_HM_FREENONPACKETIZEDSTACK].minimumSize = 
         memoryTable->memory[SCSI_HM_FREENONPACKETIZEDSTACK].memorySize =
            sizeof(SCSI_UEXACT16) * numberScbs;
   }
   memoryTable->memory[SCSI_HM_FREENONPACKETIZEDSTACK].granularity = 0;

#if SCSI_TARGET_OPERATION  
   memoryTable->memory[SCSI_HM_NEXUSQUEUE].memoryType = SCSI_MT_HPTR; /*SCSI_MT_XPTR??*/;
   memoryTable->memory[SCSI_HM_NEXUSQUEUE].memoryCategory = SCSI_MC_UNLOCKED;
   memoryTable->memory[SCSI_HM_NEXUSQUEUE].memoryAlignment = (SCSI_UEXACT8)0x03;
   memoryTable->memory[SCSI_HM_NEXUSQUEUE].minimumSize = 
      memoryTable->memory[SCSI_HM_NEXUSQUEUE].memorySize = 
         sizeof(SCSI_NEXUS) * numberNexusHandles; 
   memoryTable->memory[SCSI_HM_NEXUSQUEUE].granularity = 0;
  
   memoryTable->memory[SCSI_HM_NODEQUEUE].memoryType = SCSI_MT_HPTR; /*SCSI_MT_NPTR??*/;
   memoryTable->memory[SCSI_HM_NODEQUEUE].memoryCategory = SCSI_MC_UNLOCKED;
   memoryTable->memory[SCSI_HM_NODEQUEUE].memoryAlignment = (SCSI_UEXACT8)0x03;
   memoryTable->memory[SCSI_HM_NODEQUEUE].minimumSize = 
      memoryTable->memory[SCSI_HM_NODEQUEUE].memorySize = 
         sizeof(SCSI_NODE) * numberNodeHandles; 
   memoryTable->memory[SCSI_HM_NODEQUEUE].granularity = 0; /* still to add the node table stuff */
#endif /* SCSI_TARGET_OPERATION */ 
}

/*********************************************************************
*
*  SCSIRApplyMemoryTable
*
*     This routine will apply memory pointers described in memory table.
*
*  Return Value:  SCSI_SUCCESS - memory table get applied
*                 SCSI_FAILURE - memory table not applicable
*                  
*  Parameters:    hhcb
*                 memoryTable
*
*  Remarks:       Memory pointers will be applied and saved in 
*                 associated hhcb. After this call the memory table 
*                 is not required (e.g. can be free) from now on.
*                 
*                 Memory pointers in memory table must be setup
*                 properly to satify the memory requirement 
*                 before this routine get called.
*
*********************************************************************/
SCSI_UINT8 SCSIRApplyMemoryTable (SCSI_HHCB SCSI_HPTR hhcb,
                                  SCSI_MEMORY_TABLE SCSI_HPTR memoryTable)
{
#if (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION)
   hhcb->SCSI_FREE_PACKETIZED_STACK = (SCSI_UEXACT16 SCSI_HPTR)
                        memoryTable->memory[SCSI_HM_FREEPACKETIZEDSTACK].ptr.hPtr;
#endif /* (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION) */
   hhcb->SCSI_FREE_NON_PACKETIZED_STACK = (SCSI_UEXACT16 SCSI_HPTR)
                        memoryTable->memory[SCSI_HM_FREENONPACKETIZEDSTACK].ptr.hPtr;
#if SCSI_TARGET_OPERATION 
   /* Assign Nexus Queue */
   hhcb->SCSI_HF_targetNexusQueue = (SCSI_NEXUS SCSI_XPTR) 
                        memoryTable->memory[SCSI_HM_NEXUSQUEUE].ptr.hPtr;

#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT   
   /* Save the memory area start. */
   hhcb->SCSI_HF_targetNexusMemory = hhcb->SCSI_HF_targetNexusQueue;
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */   

   /* Assign Node Queue */
   hhcb->SCSI_HF_targetNodeQueue = (SCSI_NODE SCSI_NPTR)
                        memoryTable->memory[SCSI_HM_NODEQUEUE].ptr.hPtr;
#endif /* SCSI_TARGET_OPERATION */

   return(SCSIHApplyMemoryTable(hhcb,memoryTable));
}

/*********************************************************************
*
*  SCSIRInitialize
*
*     Initialize hardware based on configuration information 
*     described 
*
*  Return Value:  SCSI_SUCCESS           - initialization successful
*                 SCSI_SEQUENCER_FAILURE - sequencer not functional.
*                 SCSI_TIMEOUT_FAILURE   - A timeout occurred while
*                                          waiting for bus to be
*                                          stable after a chip reset.
*                 SCSI_BUS_HUNG_FAILURE  - The SCSI bus is hung.
*                  
*  Parameters:    hhcb
*
*  Remarks:       Usually this routine is called after SCSIRGetConfiguration,
*                 SCSIRGetMemoryTable and SCSIRSetMemoryTable. It requires 
*                 both configuration and memory available in order to 
*                 initialize hardware properly.
*                 
*********************************************************************/
SCSI_UINT8 SCSIRInitialize (SCSI_HHCB SCSI_HPTR hhcb)
{
   /* general reset */
   SCSIrReset(hhcb);

   /* initialize hardware management layer */
   return(SCSIHInitialize(hhcb));
}
                     
/*********************************************************************
*
*  SCSIrReset
*
*     Reset for resource management layer software
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
void SCSIrReset (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_UEXACT16 i;
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl;
   SCSI_UEXACT16 maxDev = SCSI_MAXDEV;

   /* initialize done queue */
   hhcb->SCSI_DONE_HEAD = hhcb->SCSI_DONE_TAIL = SCSI_NULL_HIOB;

   /* initialize scb waiting queue */
   hhcb->SCSI_WAIT_HEAD = hhcb->SCSI_WAIT_TAIL = SCSI_NULL_HIOB;

   /* Initialize the packetized and the non-packetized stack of SCB numbers */
   /* Actual number of SCBs that can be allocated is one less, as 0xFF is   */
   /* used by the sequencer to identify an invalid SCB in the busy targets  */
   /* table. */
   if (hhcb->numberScbs != 1)
   {
      i = hhcb->numberScbs - 1;
#if (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION)
      for (; i > (SCSI_UEXACT16)255; i--)
      {
         hhcb->SCSI_FREE_PACKETIZED_STACK[i-(SCSI_UEXACT16)256] = i;
      }
#endif /* (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION) */

      /* One SCB # has to be removed due to the one-DMA delivery scheme */
      SCSI_SET_NULL_SCB(hhcb->SCSI_FREE_NON_PACKETIZED_STACK[i]);
      /* Also ensures SCB # 0xFF is never used - reserved for the BIOS */ 
      i--;
      for (; i != 0; i--)
      {
         hhcb->SCSI_FREE_NON_PACKETIZED_STACK[i] = i;
      }
   }
   hhcb->SCSI_FREE_NON_PACKETIZED_STACK[0] = 0;

   /* Reset Target Mode RSM layer structures */
   SCSI_rRESETTARGETMODE(hhcb);

   SCSI_rREMOVESCBS(hhcb);
      
   /* initialize the target control table */
   for (i = 0,targetControl = hhcb->SCSI_HP_targetControl; i < maxDev;
        i++,targetControl++)
   {
      targetControl->maxNonTagScbs = (SCSI_UINT16) hhcb->maxNonTagScbs;
      targetControl->maxTagScbs = (SCSI_UINT16) hhcb->maxTagScbs;
      targetControl->queueHead = targetControl->queueTail = SCSI_NULL_HIOB;
      targetControl->activeScbs = 0;
      targetControl->origMaxNonTagScbs = 0;
      targetControl->origMaxTagScbs = 0;
      targetControl->activeTRs = 0;
      targetControl->activeABTTSKSETs = 0;
      targetControl->freezeMap = 0;
      targetControl->freezeTail = SCSI_NULL_HIOB;
      targetControl->activeHiPriScbs = 0;
      targetControl->deviceQueueHiobs = 0;
#if SCSI_NEGOTIATION_PER_IOB
      targetControl->pendingNego = 0;
#endif /* SCSI_NEGOTIATION_PER_IOB */
   }
}

/*********************************************************************
*
*  SCSIrStandardRemoveScbs
*
*     Remove Scbs for standard modes
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:  To be implemented.
*                  
*********************************************************************/
#if (SCSI_STANDARD_U320_MODE || SCSI_DCH_U320_MODE)
void SCSIrStandardRemoveScbs (SCSI_HHCB SCSI_HPTR hhcb)
{
}
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_DCH_U320_MODE) */

/*********************************************************************
*
*  SCSIrStandardEnhu320RemoveScbs
*
*     Remove Scbs for standard enahnced mode
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:  To be implemented.
*                  
*********************************************************************/
#if SCSI_STANDARD_ENHANCED_U320_MODE
void SCSIrStandardEnhU320RemoveScbs (SCSI_HHCB SCSI_HPTR hhcb)
{
#if SCSI_TARGET_OPERATION
#if (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)

   /* Remove an SCB for packetized target mode exception handling */
   if (hhcb->SCSI_HF_targetMode)
   {
      if (hhcb->numberScbs > (SCSI_UEXACT16)256)
      {
         hhcb->SCSI_HF_targetErrorScb =
            hhcb->SCSI_FREE_PACKETIZED_STACK[hhcb->SCSI_FREE_PACKETIZED_PTR];
         SCSI_SET_NULL_SCB(
            hhcb->SCSI_FREE_PACKETIZED_STACK[hhcb->SCSI_FREE_PACKETIZED_PTR]);
         hhcb->SCSI_FREE_PACKETIZED_PTR =
            (SCSI_UEXACT8)
            (((SCSI_UEXACT16)(++(hhcb->SCSI_FREE_PACKETIZED_PTR))) %
            (hhcb->numberScbs - (SCSI_UEXACT16)256));
      }

      /* If the packetized stack is empty or does not exist
       * allocate the SCB number from the non-packetized stack.
       */
      if (SCSI_IS_NULL_SCB(hhcb->SCSI_HF_targetErrorScb))
      {
         hhcb->SCSI_HF_targetErrorScb = 
            hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR];
         SCSI_SET_NULL_SCB(
            hhcb->SCSI_FREE_NON_PACKETIZED_STACK[hhcb->SCSI_FREE_NON_PACKETIZED_PTR]);
         hhcb->SCSI_FREE_NON_PACKETIZED_PTR =
            (SCSI_UEXACT8)
            (((SCSI_UEXACT16)(++(hhcb->SCSI_FREE_NON_PACKETIZED_PTR))) %
            hhcb->numberScbs);
      }

      if (!(SCSI_IS_NULL_SCB(hhcb->SCSI_HF_targetErrorScb)))
      {
         hhcb->SCSI_NON_EST_SCBS_AVAIL--;
      } 
   }
#endif /* (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */
#endif /* SCSI_TARGET_OPERATION */
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/*********************************************************************
*
*  SCSIrResetTargetMode
*
*     Reset the target mode portion of the resource management layer  
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
#if SCSI_TARGET_OPERATION
void SCSIrResetTargetMode (SCSI_HHCB SCSI_HPTR hhcb)
{
   if (hhcb->SCSI_HF_targetMode)
   {
      /* Initialize some target mode structures */
      /* Initialize nexus pointers */ 
      SCSIrInitializeNexusQueue(hhcb);
      /* Initialize node pointers */
      SCSIrInitializeNodeQueue(hhcb);

      /* Null the busHeldScbNumber */
      SCSI_SET_NULL_SCB(hhcb->SCSI_HP_busHeldScbNumber);

      /* Initialize the counter of number of SCBs that may be used for 
       * non establish conections (i.e. SCBs that can be
       * used for initiator or target mode reestablish connection
       * requests).
       */
      /* One SCB reserved due to one DMA delivery scheme. */ 
      hhcb->SCSI_NON_EST_SCBS_AVAIL = (hhcb->numberScbs - 1) -
                                      hhcb->SCSI_HF_targetNumberEstScbs;

      /* Initialize the counter for the number EST SCBs available. */
      hhcb->SCSI_EST_SCBS_AVAIL = hhcb->SCSI_HF_targetNumberEstScbs;
   }

   OSDDebugPrint(8,("\n SCSI_EST_SCBS_AVAIL = %u ",
                    hhcb->SCSI_EST_SCBS_AVAIL));
   OSDDebugPrint(8,("\n SCSI_NON_EST_SCBS_AVAIL = %u ",
                    hhcb->SCSI_NON_EST_SCBS_AVAIL));
}
#endif /* SCSI_TARGET_OPERATION */

/*********************************************************************
*
*  SCSIrTargetGetConfiguration
*
*     Get target mode specific configuration fields. 
*     Currently just returns the number of establish connection scbs.  
*
*  Return Value:  None
*
*  Parameters:    hhcb        - pointer to hhcb structure
*
*  Remarks:       None.
*
*********************************************************************/
#if SCSI_TARGET_OPERATION
void SCSIrTargetGetConfiguration (SCSI_HHCB SCSI_HPTR hhcb)
{

#if SCSI_ESTABLISH_CONNECTION_SCBS
   hhcb->SCSI_HF_targetNumberEstScbs = SCSI_ESTABLISH_CONNECTION_SCBS;
#else
   hhcb->SCSI_HF_targetNumberEstScbs = 0; 
#endif /* SCSI_ESTABLISH_CONNECTION_SCBS */
   if (hhcb->SCSI_HF_targetNumberEstScbs == 0)
   {
      /* Use a default value - approx 1/3 of SCBs */  
      hhcb->SCSI_HF_targetNumberEstScbs = hhcb->numberScbs / 3;
   }

   OSDDebugPrint(8,("\n NumberEstScbs = %u ",
                    hhcb->SCSI_HF_targetNumberEstScbs));
   OSDDebugPrint(8,("\n numberScbs = %u ",
                    hhcb->numberScbs));
}
#endif /* SCSI_TARGET_OPERATION */

/*********************************************************************
*
*  SCSIrInitializeNexusQueue
*
*     Initialize the Nexus Queue  
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
#if SCSI_TARGET_OPERATION
void SCSIrInitializeNexusQueue (SCSI_HHCB SCSI_HPTR hhcb)
{

   SCSI_NEXUS SCSI_XPTR nexus;
   SCSI_UEXACT16 i;
   SCSI_UEXACT16 nexusHandleCnt = hhcb->SCSI_HF_targetNumberNexusHandles; 
   
   if (nexusHandleCnt != 0)
   {
      for (i = 0, nexus = hhcb->SCSI_HF_targetNexusQueue; i < nexusHandleCnt;
           i++, nexus++)
      {
         /* Assign HHCB reference to nexus */
         nexus->hhcb = hhcb;

         /* Mark nexus available */
         nexus->SCSI_XF_available = 1;
      
         if ((i+1) == nexusHandleCnt)
         {
            /* Last element in queue is NULL */
            nexus->nextNexus = SCSI_NULL_NEXUS;
         }
         else
         {
            /* Link the nexus */
            nexus->nextNexus = nexus+1;
         }
      }
   }
   else
   {
      hhcb->SCSI_HF_targetNexusQueue = SCSI_NULL_NEXUS;
   }
       
   /* set available count */
   hhcb->SCSI_HF_targetNexusQueueCnt = nexusHandleCnt;
   
}
#endif /* SCSI_TARGET_OPERATION */

/*********************************************************************
*
*  SCSIrInitializeNodeQueue
*
*     Initialize the Node Queue  
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:
*     The node queue contains a pool of available node structures which
*     are requested by the HWM when a selection of a previously unknown
*     initiator occurs. The size of this pool is dependent on the value
*     of targetNumNodeTaskSetHandles.    
*                  
*********************************************************************/
#if SCSI_TARGET_OPERATION
void SCSIrInitializeNodeQueue (SCSI_HHCB SCSI_HPTR hhcb)
{

   SCSI_NODE SCSI_NPTR node;
   SCSI_UEXACT16 i;
   SCSI_UEXACT16 nodeHandleCnt = hhcb->SCSI_HF_targetNumberNodeHandles; 
   
   for (i = 1,node = hhcb->SCSI_HF_targetNodeQueue; i < nodeHandleCnt;
        i++,node++)
   {
      node->nextNode = node+1;
   }

   if (nodeHandleCnt != 0)
   {
      node->nextNode = SCSI_NULL_NODE;
   }
   else
   {
      hhcb->SCSI_HF_targetNodeQueue = SCSI_NULL_NODE;
   }
     
}
#endif /* SCSI_TARGET_OPERATION */




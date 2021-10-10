/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 *$Header: Z:\u320chim\src\chim\xlm\xlminit.c   \main\49   Mon Jun 23 08:17:03 2003   mat22316 $
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
#pragma ident	"%Z%%M%	%I%	%E% SMI"

/***************************************************************************
*
*  Module Name:   XLMINIT.C
*
*  Description:
*                 Codes to implement Common HIM interface for its 
*                 initialization
*
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         NONE
*
*  Entry Point(s):
*                 SCSIGetFunctionPointers
*                 SCSIGetNextHostDeviceType
*                 SCSICreateInitializationTSCB
*                 SCSIGetConfiguration
*                 SCSISetConfiguration
*                 SCSISizeAdapterTSCB
*                 SCSICreateAdapterTSCB
*                 SCSISetupAdapterTSCB
*                 SCSICheckMemoryNeeded
*                 SCSISetMemoryPointer
*                 SCSIVerifyAdapter
*                 SCSIInitialize
*                 SCSISizeTargetTSCB
*                 SCSICheckTargetTSCBNeeded
*                 SCSICreateTargetTSCB
*
***************************************************************************/

#define  SCSI_XLM_DEFINE

#include "scsi.h"
#include "xlm.h"

/*********************************************************************
*
*  SCSIGetFunctionPointers
*
*     Get default configuration information
*
*  Return Value:  none
*                  
*  Parameters:    himFunctions
*                 length
*
*  Remarks:       This routine will fill function pointer as interface
*                 to CHIM. This is the first routine get called by OSM.
*
*********************************************************************/
void SCSIGetFunctionPointers (HIM_FUNC_PTRS HIM_PTR himFunctions,
                              HIM_UINT16 length)
{
   /* copy scsi chim function pointer table over */
   OSDmemcpy(himFunctions,&SCSIFuncPtrs,length);
}

/*********************************************************************
*
*  SCSIGetNextHostDeviceType
*
*     This routine returns the next host device type supported by
*     SCSI CHIM.
*
*  Return Value:  0 - No more device type
*                 host id
*                  
*  Parameters:    index
*                 hostBusType
*                 mask
*
*  Remarks:       SCSI CHIM will make a host device type table built
*                 at compile time. Parameter index will be used to
*                 index into that table. The table itself should be
*                 static at initialization/run time. The table is
*                 maintained at CHIM layer.
*
*********************************************************************/
HIM_HOST_ID SCSIGetNextHostDeviceType (HIM_UINT16 index,
                                       HIM_UINT8 HIM_PTR hostBusType,
                                       HIM_HOST_ID HIM_PTR mask)
{
   HIM_HOST_ID hostID = 0;

   /* make sure initialization TSCB is large enough */
   if ((HIM_SCSI_INIT_TSCB_SIZE >= sizeof(SCSI_INITIALIZATION_TSCB)) &&
       (sizeof(SCSI_NVM_LAYOUT) == 64) &&
       (sizeof(SCSI_SCBFF) == 64))
   {
      /* make sure index is valid to index into the table */
      /* and the entry is not the delimeter.              */
      if ((index < (sizeof(SCSIHostType) / sizeof(SCSI_HOST_TYPE))) &&
          (SCSIHostType[index].idHost != 0))
      {
         *hostBusType = HIM_HOST_BUS_PCI;
         hostID = SCSIHostType[index].idHost;
         *mask = SCSIHostType[index].idMask;
      }
   }

   return(hostID);
}

/*********************************************************************
*
*  SCSICreateInitializationTSCB
*
*     This routine pass the initializationTSCB from OSM to CHIM.
*
*  Return Value:  initialization TSH
*                  
*  Parameters:    pointer to initialization TSCB
*
*  Remarks:       This routine will basically format/prepare the
*                 memory (pointed by initializationTSCB) ready to
*                 continue the initialization process. The pointer
*                 to initialization TSCB will be returned as
*                 initialization TSH. 
*
*********************************************************************/
HIM_TASK_SET_HANDLE SCSICreateInitializationTSCB (void HIM_PTR pHIMInitTSCB)
{
   SCSI_MEMORY_TABLE HIM_PTR memoryTable = SCSI_INITSH(pHIMInitTSCB)->memoryTable;

   /* set TSCB type */
   SCSI_INITSH(pHIMInitTSCB)->typeTSCB = SCSI_TSCB_TYPE_INITIALIZATION;

   /* copy default configuration setup */
   OSDmemcpy(&SCSI_INITSH(pHIMInitTSCB)->configuration,
            &SCSIConfiguration,sizeof(HIM_CONFIGURATION));

   /* get memory table for all sequencer modes */
   /* assume maximum number of scbs required */

   /* Maximum memory cannot be determined for target mode */
   /* operation until SCSISetConfiguration is invoked.    */
   /* Therefore, just set the nexusTSH and nodeTSH        */
   /* parameters to 0.                                    */

#if SCSI_STANDARD_U320_MODE
   SCSI_GET_MEMORY_TABLE(SCSI_FMODE_STANDARD_U320,
                         SCSI_MAXSCBS,
                         0,
                         0,
                         &memoryTable[0]);
#else
   OSDmemset(&memoryTable[0],0,sizeof(SCSI_MEMORY_TABLE));
#endif /* SCSI_STANDARD_U320_MODE */

#if SCSI_STANDARD_ENHANCED_U320_MODE
   SCSI_GET_MEMORY_TABLE(SCSI_FMODE_STANDARD_ENHANCED_U320,
                         SCSI_MAXSCBS,
                         0,
                         0,
                         &memoryTable[1]);
#else
   OSDmemset(&memoryTable[1],0,sizeof(SCSI_MEMORY_TABLE));
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

   SCSIxMaximizeMemory(&memoryTable[0],&memoryTable[1]);

   return((HIM_TASK_SET_HANDLE) pHIMInitTSCB);
}

/*********************************************************************
*
*  SCSIxMaximizeMemory
*
*     Get the worst memory requirement of both memory table
*
*  Return Value:  none
*                  
*  Parameters:    memoryTable 1
*                 memoryTable 2
*
*  Remarks:       memoryTable 2 should never be overwritten!
*
*********************************************************************/
void SCSIxMaximizeMemory (SCSI_MEMORY_TABLE HIM_PTR memoryTable1,
                          SCSI_MEMORY_TABLE HIM_PTR memoryTable2)
{
   SCSI_UINT8 i;

   /* assume the worst memory requirements */
   for (i = 0; i < SCSI_MAX_MEMORY; i++)
   {
      /* Take the worst size */
      if (memoryTable1->memory[i].memorySize <
          memoryTable2->memory[i].memorySize)
      {
         memoryTable1->memory[i].memorySize =
            memoryTable2->memory[i].memorySize;
      }

      /* take the worst alignment */
      if (memoryTable1->memory[i].memoryAlignment <
          memoryTable2->memory[i].memoryAlignment)
      {
         memoryTable1->memory[i].memoryAlignment =
            memoryTable2->memory[i].memoryAlignment;
      }
   }
}

/*********************************************************************
*
*  SCSIGetConfiguration
*
*     Get the default configuration information
*
*  Return Value:  none
*                  
*  Parameters:    initialization TSH
*                 config structure
*                 product id
*
*  Remarks:       This routine will get the default configuration based
*                 on product id available. A particular hardware
*                 device has not been identified so there is no way
*                 to access to the hardware at the moment.
*
*********************************************************************/
void SCSIGetConfiguration (HIM_TASK_SET_HANDLE initializationTSH,
                           HIM_CONFIGURATION HIM_PTR pConfig,     
                           HIM_HOST_ID productID)
{
   /* copy the current configuration setup over */
   OSDmemcpy(pConfig,&SCSI_INITSH(initializationTSH)->configuration,
      sizeof(HIM_CONFIGURATION));
}

/*********************************************************************
*
*  SCSISetConfiguration
*
*     Apply the configuration based on associaed config structure
*
*  Return Value:  HIM_SUCCESS - updates applied
*                 HIM_FAILURE - productID was invalid
*                 HIM_ILLEGAL_CHANGE - illegal change
*                  
*  Parameters:    initialization TSH
*                 config structure
*                 product id
*
*  Remarks:       This routine will apply the configuration specified
*                 with config structure and keep it in the
*                 initialization TSCB. Hardware is still not available
*                 the moment. 
*
*********************************************************************/
HIM_UINT8 SCSISetConfiguration (HIM_TASK_SET_HANDLE initializationTSH,
                                HIM_CONFIGURATION HIM_PTR pConfig,     
                                HIM_HOST_ID productID)
{
   SCSI_MEMORY_TABLE HIM_PTR memoryTable = SCSI_INITSH(initializationTSH)->memoryTable;
   HIM_UINT8 status = HIM_ILLEGAL_CHANGE;
   SCSI_UEXACT16 nexusHandles = 0;
   SCSI_UEXACT8 nodeHandles = 0; 

   /* Check the following for a valid request:                      */
   /*     make sure number of scbs is within the valid range        */
   /*     memoryMapped < HIM_MIXED_RANGES                          */
   /*     targetMode = 1 OR/AND initiatorMode = 1                   */
   /*     if targetMode = 1 then                                    */
   /*             0 < targetMaxNexusHandles <= SCSI_MAXNEXUSHANDLES */      
   /*             0 < targetMaxNodeHandles <= SCSI_MAXNODES         */
   /*             0 < targetNumIDs <= HIM_MAX_SCSI_ADAPTER_IDS      */
   
   if ((pConfig->maxInternalIOBlocks <= SCSI_MAXSCBS) &&
       (pConfig->maxInternalIOBlocks >= SCSI_MINSCBS) &&
       (pConfig->iobReserveSize == SCSIConfiguration.iobReserveSize) &&
       (pConfig->memoryMapped < HIM_MIXED_RANGES) && 
       (pConfig->initiatorMode || pConfig->targetMode) &&
#if (SCSI_INITIATOR_OPERATION && !SCSI_TARGET_OPERATION)
       (pConfig->targetMode == HIM_FALSE) &&
#endif /* (SCSI_INITIATOR_OPERATION && (!SCSI_TARGET_OPERATION)) */
#if (SCSI_TARGET_OPERATION && !SCSI_INITIATOR_OPERATION)
       (pConfig->initiatorMode == HIM_FALSE) &&
#endif /* (SCSI_TARGET_OPERATION && (!SCSI_INITIATOR_OPERATION)) */
       (
#if SCSI_TARGET_OPERATION
        (pConfig->targetMode && 
         (pConfig->targetNumNexusTaskSetHandles > 0) &&
         (pConfig->targetNumNexusTaskSetHandles <= SCSI_MAXNEXUSHANDLES) &&
#if SCSI_MULTIPLEID_SUPPORT
         (pConfig->targetNumIDs > 0) &&
         (pConfig->targetNumIDs <= HIM_MAX_SCSI_ADAPTER_IDS) &&
#else
         (pConfig->targetNumIDs == 1) &&
#endif /* SCSI_MULTIPLEID_SUPPORT */
         (pConfig->targetNumNodeTaskSetHandles > 0) &&
         (pConfig->targetNumNodeTaskSetHandles <= SCSI_MAXNODES)) ||
#endif /* SCSI_TARGET_OPERATION */
         ((!pConfig->targetMode))))
   {
#if SCSI_TARGET_OPERATION
      if (pConfig->targetMode == HIM_TRUE)
      {
         nexusHandles = (SCSI_UEXACT16)pConfig->targetNumNexusTaskSetHandles;
         nodeHandles = (SCSI_UEXACT8)pConfig->targetNumNodeTaskSetHandles;
      } 
#endif /* SCSI_TARGET_OPERATION */
       
      /* update memory allocation table if maxInternalIOBlocks,     */
      /* targetMaxNexusHandles, or targetMaxNodeHandles get changed */
     
      if ((pConfig->maxInternalIOBlocks !=
           SCSI_INITSH(initializationTSH)->configuration.maxInternalIOBlocks) ||
          (nexusHandles != 
           SCSI_INITSH(initializationTSH)->configuration.targetNumNexusTaskSetHandles) ||
          (nodeHandles !=
           SCSI_INITSH(initializationTSH)->configuration.targetNumNodeTaskSetHandles))
      {
         
#if SCSI_STANDARD_U320_MODE
         SCSI_GET_MEMORY_TABLE(SCSI_FMODE_STANDARD_U320,
                  (SCSI_UEXACT8) pConfig->maxInternalIOBlocks,
                  nexusHandles,
                  nodeHandles,
                  &memoryTable[0]);
#else
         OSDmemset(&memoryTable[0],0,sizeof(SCSI_MEMORY_TABLE));
#endif /* SCSI_STANDARD_U320_MODE */

#if SCSI_STANDARD_ENHANCED_U320_MODE
         SCSI_GET_MEMORY_TABLE(SCSI_FMODE_STANDARD_ENHANCED_U320,
                  (SCSI_UEXACT8) pConfig->maxInternalIOBlocks,
                  nexusHandles,
                  nodeHandles,
                  &memoryTable[1]);
#else
         OSDmemset(&memoryTable[1],0,sizeof(SCSI_MEMORY_TABLE));
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

         /* keep standard mode in position 1 */
         SCSIxMaximizeMemory(&memoryTable[0],&memoryTable[1]);
      }

      /* keep configuration setup up to date */
      OSDmemcpy(&SCSI_INITSH(initializationTSH)->configuration,
               pConfig,sizeof(HIM_CONFIGURATION));

      SCSI_INITSH(initializationTSH)->configuration.targetNumNexusTaskSetHandles =
          nexusHandles;
      SCSI_INITSH(initializationTSH)->configuration.targetNumNodeTaskSetHandles =
          nodeHandles;

      /* Doesn't affect memory requirements so just copy passed in value. */ 
      SCSI_INITSH(initializationTSH)->configuration.targetNumIDs =
          pConfig->targetNumIDs;

      status = HIM_SUCCESS;
   }

   return(status);
}

/*********************************************************************
*
*  SCSISizeAdapterTSCB
*
*     Get the size requirement for adapter TSCB
*
*  Return Value:  size required for adapter TSCB
*                  
*  Parameters:    initialization TSH
*                 product id
*
*  Remarks:       This routine may take advantage of configuration
*                 recorded at SCSISetConfiguration call to optimize
*                 the memory required for adapter TSCB.
*
*********************************************************************/
HIM_UINT32 SCSISizeAdapterTSCB (HIM_TASK_SET_HANDLE initializationTSH,
                                HIM_HOST_ID productID)
{
   return(sizeof(SCSI_ADAPTER_TSCB));
}

/*********************************************************************
*
*  SCSICreateAdapterTSCB
*
*     Create and initialize adapter TSCB
*
*  Return Value:  adapter TSH
*                 0 - failure with adapter TSCB initialization
*                  
*  Parameters:    initialization TSH
*                 pointer to adapter TSCB
*                 pointer to osm adapter context
*                 host device address 
*                 product id
*
*  Remarks:       Information kept at initialization TSCB should be
*                 copied over to adapter TSCB. HHCB information 
*                 prepared in initialization TSCB is a typical example
*                 to be copied over to adapter TSCB.
*
*********************************************************************/
HIM_TASK_SET_HANDLE SCSICreateAdapterTSCB (HIM_TASK_SET_HANDLE initializationTSH,
                                           void HIM_PTR tscbPointer,
                                           void HIM_PTR osmAdapterContext,
                                           HIM_HOST_ADDRESS hostAddress,
                                           HIM_HOST_ID productID)
{
   SCSI_HHCB HIM_PTR hhcb = &SCSI_ADPTSH(tscbPointer)->hhcb;
   HIM_UINT16 nexusHandles = (HIM_UINT16)
      SCSI_INITSH(initializationTSH)->configuration.targetNumNexusTaskSetHandles;
#if SCSI_TARGET_OPERATION
   HIM_UINT8 nodeHandles = (HIM_UINT8)
      SCSI_INITSH(initializationTSH)->configuration.targetNumNodeTaskSetHandles; 
#endif /* SCSI_TARGET_OPERATION */      

   /* set type of TSCB */
   SCSI_ADPTSH(tscbPointer)->typeTSCB = SCSI_TSCB_TYPE_ADAPTER;
   SCSI_ADPTSH(tscbPointer)->hhcb.numberScbs = (SCSI_UEXACT16)
         SCSI_INITSH(initializationTSH)->configuration.maxInternalIOBlocks;

   SCSI_ADPTSH(tscbPointer)->scsiHandle.memoryMapped =
      SCSI_INITSH(initializationTSH)->configuration.memoryMapped;

   SCSI_ADPTSH(tscbPointer)->osmAdapterContext = osmAdapterContext;

   /* store the number of nexus and node TSH's to be allocated,
      initiatorMode, and targetMode settings */
   SCSI_ADPTSH(tscbPointer)->hhcb.SCSI_HF_initiatorMode = (SCSI_UEXACT16)
         SCSI_INITSH(initializationTSH)->configuration.initiatorMode;

   if (SCSI_INITSH(initializationTSH)->configuration.targetMode && 
       nexusHandles > 0)
   {     
      SCSI_ADPTSH(tscbPointer)->hhcb.SCSI_HF_targetMode = 1;
   }
   else
   {
      SCSI_ADPTSH(tscbPointer)->hhcb.SCSI_HF_targetMode = 0;
      nexusHandles = 0;
#if SCSI_TARGET_OPERATION      
      nodeHandles = 0;      
#endif /* SCSI_TARGET_OPERATION */      
   }

#if SCSI_TARGET_OPERATION
   SCSI_ADPTSH(tscbPointer)->hhcb.SCSI_HF_targetNumberNexusHandles =
       (SCSI_UEXACT16)nexusHandles;
   SCSI_ADPTSH(tscbPointer)->hhcb.SCSI_HF_targetNumberNodeHandles =
       (SCSI_UEXACT8)nodeHandles;
   SCSI_ADPTSH(tscbPointer)->hhcb.SCSI_HF_targetNumIDs =
       (SCSI_UEXACT8)SCSI_INITSH(initializationTSH)->configuration.targetNumIDs;
#endif /* SCSI_TARGET_OPERATION */

   hhcb->hardwareMode = SCSI_HMODE_AICU320;

   /* copy memory table over depending on product id */
   if (SCSI_IS_U320_ID(productID))
   {
      /* Note that this may be a problem as we cannot determine
       * if this is Rev B ASIC at this time. Means more memory 
       * may be allocated than required (in the case of packetized 
       * target mode).
       */
#if SCSI_STANDARD_ENHANCED_U320_MODE
      hhcb->firmwareMode = SCSI_FMODE_STANDARD_ENHANCED_U320;
      OSDmemcpy(&SCSI_ADPTSH(tscbPointer)->memoryTable,
                &SCSI_INITSH(initializationTSH)->memoryTable[1],
                sizeof(SCSI_MEMORY_TABLE));
#else
      /* SCSI_STANDARD_U320_MODE */
      hhcb->firmwareMode = SCSI_FMODE_STANDARD_U320;
      OSDmemcpy(&SCSI_ADPTSH(tscbPointer)->memoryTable,
                &SCSI_INITSH(initializationTSH)->memoryTable[0],
                sizeof(SCSI_MEMORY_TABLE));
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */
   }
   
   SCSI_ADPTSH(tscbPointer)->productID = productID;
   SCSI_ADPTSH(tscbPointer)->hostAddress = hostAddress;

#if SCSI_DOMAIN_VALIDATION
   SCSI_ADPTSH(tscbPointer)->SCSI_AF_dvInProgress = 0;
   SCSI_ADPTSH(tscbPointer)->SCSI_AF_dvFrozen = 0;
#endif /* SCSI_DOMAIN_VALIDATION */

   return((HIM_TASK_SET_HANDLE) tscbPointer);
}

/*********************************************************************
*
*  SCSISetupAdapterTSCB
*
*     Setup adapter TSCB ready to access OSM support routines
*
*  Return Value:  HIM_SUCCESS - adapter setup successful
*                 HIM_FAILURE - Unexpected value of
*                               osmFuncLength (not implemented).
*                  
*  Parameters:    adapter TSH
*                 osmRoutines
*                 osmFuncLength
*                 
*
*  Remarks:       Not until call to this routine the OSM support routines 
*                 are available for access. Those OSM prepared function
*                 pointers should be copied over to adapter TSCB for 
*                 future usage.
*
*********************************************************************/
HIM_UINT8 SCSISetupAdapterTSCB (HIM_TASK_SET_HANDLE adapterTSH,
                                HIM_OSM_FUNC_PTRS HIM_PTR osmRoutines,
                                HIM_UINT16 osmFuncLength)
{
   /* copy the osm function pointers over */
   OSDmemcpy(&SCSI_ADPTSH(adapterTSH)->osmFuncPtrs,osmRoutines,osmFuncLength);

   return((HIM_UINT8)(HIM_SUCCESS));
}

/*********************************************************************
*
*  SCSICheckMemoryNeeded
*
*     Check if more memory required by CHIM
*
*  Return Value:  size in bytes of memory required by CHIM
*                 zero indicates no more memory required 
*                  
*  Parameters:    initialization TSCB
*                 adapter TSH
*                 product id
*                 index for memory requirement 
*                 pointer to memory category
*                 minimum bytes required
*                 granularity for less memory allowed
*                 alignment mask
*
*  Remarks:       This routine may be called either before or after
*                 host device has been identified. It should make sure
*                 maximum memory size get returned to support all
*                 possible host devices. The only information this
*                 routine can take advantage of is configuration 
*                 information setup with call to SCSISetConfiguration.
*
*********************************************************************/
HIM_UINT32 SCSICheckMemoryNeeded (HIM_TASK_SET_HANDLE initializationTSH,
					HIM_TASK_SET_HANDLE	adapterTSH,
					HIM_HOST_ID	productID,
					HIM_UINT16	index,
					HIM_UINT8	HIM_PTR	category,
					HIM_UINT32	HIM_PTR	minimumBytes,
					HIM_UINT32	HIM_PTR	granularity,
					HIM_ULONG	HIM_PTR	alignmentMask)
{
   SCSI_HHCB HIM_PTR hhcb;
   SCSI_MEMORY_TABLE HIM_PTR memoryTable = HIM_NULL;
   HIM_UINT32 size = 0;
   SCSI_UEXACT16 nexusHandles = 0;
   SCSI_UEXACT8 nodeHandles = 0;

   if (adapterTSH)
   {
      /* adapterTSH is available */
      /* we should be able to further optimized memory requirement */
      memoryTable = &SCSI_ADPTSH(adapterTSH)->memoryTable;
      if (!index)
      {
         hhcb = &SCSI_ADPTSH(adapterTSH)->hhcb;

#if SCSI_TARGET_OPERATION          
         if (hhcb->SCSI_HF_targetMode)
         {
            nexusHandles = hhcb->SCSI_HF_targetNumberNexusHandles;
            nodeHandles = hhcb->SCSI_HF_targetNumberNodeHandles;
         }   
#endif /* SCSI_TARGET_OPERATION */               

         OSDmemset(memoryTable,0,sizeof(SCSI_MEMORY_TABLE));

         if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320)
         {
            /* standard Ultra 320 mode */
            SCSI_GET_MEMORY_TABLE(SCSI_FMODE_STANDARD_U320,
                                  hhcb->numberScbs,
                                  nexusHandles,
                                  nodeHandles,
                                  memoryTable);
         }
         else
         if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320)
         {
            /* standard enhanced Ultra 320 mode */
            SCSI_GET_MEMORY_TABLE(SCSI_FMODE_STANDARD_ENHANCED_U320,
                                  hhcb->numberScbs,
                                  nexusHandles,
                                  nodeHandles,
                                  memoryTable);
         }
         else
         if (hhcb->firmwareMode == SCSI_FMODE_DCH_U320)
         {
            /* standard enhanced Ultra 320 mode */
            SCSI_GET_MEMORY_TABLE(SCSI_FMODE_DCH_U320,
                                  hhcb->numberScbs,
                                  nexusHandles,
                                  nodeHandles,
                                  memoryTable);
         }

      }
   }
   else
   {
      /* memory table must be from initializationTSH */
      /* copy memory table over depending on product id */
      if (SCSI_IS_U320_ID(productID))
      {
#if SCSI_STANDARD_U320_ENHANCED_MODE
         memoryTable = &SCSI_INITSH(initializationTSH)->memoryTable[1];
#else
         /* SCSI_STANDARD_U320_MODE */
         memoryTable = &SCSI_INITSH(initializationTSH)->memoryTable[0];
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */
      }
   }

   if (index < SCSI_MAX_MEMORY)
   {
      *category = memoryTable->memory[index].memoryCategory;
      *granularity = (HIM_UINT32) memoryTable->memory[index].granularity;

      if (memoryTable->memory[index].memorySize)
      {
	/* return memory requirement information */
	size = (SCSI_UINT32) memoryTable->memory[index].memorySize;
	*minimumBytes = (SCSI_UINT32) memoryTable->memory[index].minimumSize;
	*alignmentMask = (HIM_ULONG) memoryTable->memory[index].memoryAlignment;
      }
      else
      {
         /* fake a size value so that no zero size returned */
         size = 4;
         *alignmentMask = 0;
         *minimumBytes = 0;
      }
   }
   else if (index == SCSI_INDEX_MORE_LOCKED)
   {
      *category = HIM_MC_LOCKED;
      *granularity = 0;
      *minimumBytes = size = SCSI_MORE_LOCKED;
      *alignmentMask = SCSI_MORE_LOCKED_ALIGN;
   }
   else if (index == SCSI_INDEX_MORE_UNLOCKED)
   {
      *category = HIM_MC_UNLOCKED;
      *granularity = 0;
      *minimumBytes = size = SCSI_MORE_UNLOCKED;
      *alignmentMask = SCSI_MORE_UNLOCKED_ALIGN;
   }

   return(size);
}

/*********************************************************************
*
*  SCSISetMemoryPointer
*
*     Set memory pointer for CHIM usage
*
*  Return Value:  HIM_SUCCESS, indicate memory pointer has been applied
*                 HIM_FAILURE, indicate memory pointer is not usable
*                  
*  Parameters:    adapter TSH
*                 index for memory requirement 
*                 pointer to memory category
*                 pointer to memory block
*                 size of memory block 
*
*  Remarks:       This routine should verify the memory pointer
*                 supplied by OSM is valid or not. OSM should 
*                 call this routine repetitively based on the
*                 index get used with call to SCSICheckMemoryNeeded.
*
*********************************************************************/
HIM_UINT8 SCSISetMemoryPointer (HIM_TASK_SET_HANDLE adapterTSH,
                                HIM_UINT16 index,
                                HIM_UINT8 category,
                                void HIM_PTR pMemory,
                                HIM_UINT32 size)
{
   SCSI_MEMORY_TABLE HIM_PTR memoryTable =
                              &SCSI_ADPTSH(adapterTSH)->memoryTable;
   HIM_UINT8 status = HIM_SUCCESS;

   if (index < SCSI_MAX_MEMORY)
   {
      memoryTable->memory[index].ptr.hPtr = pMemory;
      memoryTable->memory[index].memorySize = (SCSI_UEXACT16) size;
   }
   else if (index == SCSI_INDEX_MORE_LOCKED)
   {
      SCSI_ADPTSH(adapterTSH)->moreLocked = pMemory;
   }
   else if (index == SCSI_INDEX_MORE_UNLOCKED)
   {
      SCSI_ADPTSH(adapterTSH)->moreUnlocked = pMemory;
   }
   else
   {
      status = HIM_FAILURE;
   }

   return(status);   
}

/*********************************************************************
*
*  SCSIVerifyAdapter
*
*     Verify the existance of host adapter
*
*  Return Value:  HIM_SUCCESS - adapter verification successful
*                 HIM_FAILURE - adapter verification failed
*                 HIM_ADAPTER_NOT_SUPPORTED - adapter not supported
*                  
*  Parameters:    adapter TSCB
*
*  Remarks:       This routine should get all the io handles required
*                 through call to OSMMapIOHandle. With io handles 
*                 available host device can be verified.
*
*********************************************************************/
HIM_UINT8 SCSIVerifyAdapter (HIM_TASK_SET_HANDLE adapterTSH)
{
   SCSI_HHCB HIM_PTR hhcb = &SCSI_ADPTSH(adapterTSH)->hhcb;
   HIM_UINT8 status = HIM_SUCCESS;
   HIM_UINT8 i = 0;
   SCSI_UEXACT16 numberScbs;
#if !SCSI_DCH_U320_MODE
   HIM_HOST_ID hostID;
   HIM_HOST_ID subSystemSubVendorID;
   HIM_HOST_ID filteredHostID;
   HIM_HOST_ID mask;
#endif /* !SCSI_DCH_U320_MODE */
#if SCSI_PCI_COMMAND_REG_CHECK
   HIM_UEXACT32 statCmd;
#endif /* SCSI_PCI_COMMAND_REG_CHECK */
   SCSI_UEXACT8 fwMode;

#if SCSI_PCI_COMMAND_REG_CHECK
   /* get status and command from hardware */
   statCmd = OSMxReadPCIConfigurationDword(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                     SCSI_STATUS_CMD_REG);
#endif /* SCSI_PCI_COMMAND_REG_CHECK */

   /* I/O mapped */
   if (SCSI_ADPTSH(adapterTSH)->scsiHandle.memoryMapped == 
       (HIM_UINT8)HIM_IOSPACE)
   {
#if SCSI_PCI_COMMAND_REG_CHECK
      /* Return failure if I/O Space access is not enabled. */
      if (!(statCmd & SCSI_ISPACEEN))
      {
         OSMDebugPrint(1,("SCSIVerifyAdapter %d", (SCSI_UEXACT8)1));
         return((HIM_UINT8)(HIM_FAILURE)); /* should this be HIM_ADAPTER_NOT_SUPPORTED?? */
      }
#endif /* SCSI_PCI_COMMAND_REG_CHECK */
      /* Validate two handles for I/O mapped. */
      if (OSMxMapIOHandle(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                          (SCSI_UINT8)SCSI_IOMAPPED_INDEX1,
                          (SCSI_UINT32)SCSI_IOHANDLE_OFFSET,
                          (SCSI_UINT32)SCSI_IOMAPPED_HANDLE_LENGTH,
                          (SCSI_UINT32)SCSI_IOHANDLE_NO_PACING,
                          (SCSI_UINT32)HIM_IO_LITTLE_ENDIAN,
                          &SCSI_ADPTSH(adapterTSH)->scsiHandle.ioHandle) ||
          OSMxMapIOHandle(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                          (SCSI_UINT8)SCSI_IOMAPPED_INDEX2,
                          (SCSI_UINT32)SCSI_IOHANDLE_OFFSET,
                          (SCSI_UINT32)SCSI_IOMAPPED_HANDLE_LENGTH,
                          (SCSI_UINT32)SCSI_IOHANDLE_NO_PACING,
                          (SCSI_UINT32)HIM_IO_LITTLE_ENDIAN,
                          &SCSI_ADPTSH(adapterTSH)->scsiHandle.ioHandleHigh))
      {
         /* Failed */
         OSMDebugPrint(1,("SCSIVerifyAdapter %d", (SCSI_UEXACT8)2));
         return((HIM_UINT8)HIM_ADAPTER_NOT_SUPPORTED);
      }
   }
   else   /* Memory mapped */
   {
#if SCSI_PCI_COMMAND_REG_CHECK
      /* Return failure if Memory Space access is not enabled. */
      if (!(statCmd & SCSI_MSPACEEN))
      {
         OSMDebugPrint(1,("SCSIVerifyAdapter %d", (SCSI_UEXACT8)3));
         return((HIM_UINT8)(HIM_FAILURE)); /* should this be HIM_ADAPTER_NOT_SUPPORTED?? */
      }
#endif /* SCSI_PCI_COMMAND_REG_CHECK */
      /* Validate single io handle for memory mapped. */
      if (OSMxMapIOHandle(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                          (SCSI_UINT8)SCSI_MEMORYMAPPED_INDEX,
                          (SCSI_UINT32)SCSI_IOHANDLE_OFFSET,
                          (SCSI_UINT32)SCSI_MEMMAPPED_HANDLE_LENGTH,
                          (SCSI_UINT32)SCSI_IOHANDLE_NO_PACING,
                          (SCSI_UINT32)HIM_IO_LITTLE_ENDIAN,
                          &SCSI_ADPTSH(adapterTSH)->scsiHandle.ioHandle))
      {
         /* Failed */
         OSMDebugPrint(1,("SCSIVerifyAdapter %d", (SCSI_UEXACT8)4));
         return((HIM_UINT8)HIM_ADAPTER_NOT_SUPPORTED);
      }
   }

#if SCSI_PCI_COMMAND_REG_CHECK
   /* Check if bus master enabled.  And enable it if it's not. */
   if (!(statCmd & SCSI_MASTEREN))
   {
      OSMxWritePCIConfigurationDword(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
               SCSI_STATUS_CMD_REG,(statCmd | SCSI_MASTEREN));
   }
#endif /* SCSI_PCI_COMMAND_REG_CHECK */

   /* verify data parity error detection enabled */
#if (!SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING && !SCSI_DCH_U320_MODE)
   if (!(statCmd & SCSI_PERRESPEN))
   {
      OSMxWritePCIConfigurationDword(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
               SCSI_STATUS_CMD_REG,(statCmd | SCSI_PERRESPEN));
   }
#endif /* (!SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING && !SCSI_DCH_U320_MODE) */

   /* prepare scsiHandle for internal HIM access */
   SCSI_ADPTSH(adapterTSH)->scsiHandle.OSMReadUExact8 = OSMxReadUExact8;
   SCSI_ADPTSH(adapterTSH)->scsiHandle.OSMReadUExact16 = OSMxReadUExact16;
   SCSI_ADPTSH(adapterTSH)->scsiHandle.OSMWriteUExact8 = OSMxWriteUExact8;
   SCSI_ADPTSH(adapterTSH)->scsiHandle.OSMWriteUExact16 = OSMxWriteUExact16;
#if !SCSI_IO_SYNCHRONIZATION_GUARANTEED
   SCSI_ADPTSH(adapterTSH)->scsiHandle.OSMSynchronizeRange = OSMxSynchronizeRange;
#else /* SCSI_IO_SYNCHRONIZATION_GUARANTEED */
   SCSI_ADPTSH(adapterTSH)->scsiHandle.OSMSynchronizeRange = 0;
#endif /* !SCSI_IO_SYNCHRONIZATION_GUARANTEED */

   /* Now that registers invalidate the logical current MODE_PTR 
    * register value.
    */
   hhcb->SCSI_HP_currentMode = SCSI_MODEPTR_UNDEFINED;

#if !SCSI_DCH_U320_MODE

   /* get device id from hardware */
   hostID = OSMxReadPCIConfigurationDword(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                                          SCSI_ID_REG);

   subSystemSubVendorID = 
      OSMxReadPCIConfigurationDword(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                                    SCSI_SSID_SVID_REG);

   if (SCSI_IS_U320_ID(hostID))
   {
      /* Check if U320 device is a 'dual-channel' chip by checking */
      /* the multi-function bit in PCI header type config space.   */
      if ((OSMxReadPCIConfigurationDword(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
          SCSI_CACHE_LAT_HDR_REG) & SCSI_HDRTYPE_MULTIFUNC) == SCSI_HDRTYPE_MULTIFUNC);
      {
         /* The ASIC revision that is supported for U320 'dual-channel' chip */
         /* should be greater than or equal to 1. If it is less than that    */
         /* return a failure status of 'host adapter not supported'. */
         if ((SCSI_UEXACT8)OSMxReadPCIConfigurationDword(
               SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
               SCSI_DEV_REV_ID_REG) < SCSI_HARPOON2_REV_3_CHIP)
         {
            OSMDebugPrint(1,("SCSIVerifyAdapter %d", (SCSI_UEXACT8)5));
            return((HIM_UINT8)HIM_ADAPTER_NOT_SUPPORTED);
         }
      }

      /* If U320 type then the subsystem ID is programable according to      */
      /* the new PCI device ID spec.  This subsystem ID value can be various */
      /* combination depending on the feature of the hardware.  We need to   */
      /* have a way to filter out a device in which we don't want to support */
      /* this hardware.  SCSIxU320GetNextSubSystemSubVendorID is called to   */
      /* determine which subsystem IDs are not supported.                    */
      i = 0;
      while(1)
      {
         if ((filteredHostID = 
              SCSIxU320GetNextSubSystemSubVendorID(i,&mask)) == 0)
         {
            break;      /* If reach the end w/o match, continue with rest of routine. */
         }
         i++;
         if ((mask & subSystemSubVendorID) == filteredHostID)
         {
            /* Device found, then return HA is not supported. */
            OSMDebugPrint(1,("SCSIVerifyAdapter %d", (SCSI_UEXACT8)6));
            return((HIM_UINT8)HIM_ADAPTER_NOT_SUPPORTED);
         }
      }
   }

   /* make sure device id matched */
   if (SCSI_ADPTSH(adapterTSH)->productID == hostID)
   {
      /* setup hhcb before it's ready to be used */
      hhcb->scsiRegister = &SCSI_ADPTSH(adapterTSH)->scsiHandle;

      /* setup build SCB function pointer depending on operating mode */
      /* return from SCSIxDetermineFirmwareMode routine. */
         
      fwMode = SCSIxDetermineFirmwareMode(SCSI_ADPTSH(adapterTSH));

#if SCSI_STANDARD_U320_MODE
      if (fwMode == (SCSI_UEXACT8)SCSI_FMODE_STANDARD_U320)
      {
         /* Standard Ultra 320 mode */
         hhcb->firmwareMode = SCSI_FMODE_STANDARD_U320;
         SCSI_ADPTSH(adapterTSH)->OSDBuildSCB = OSDStandardU320BuildSCB;
#if SCSI_TARGET_OPERATION
         /* setup build SCB routine function pointer for target operation */
         SCSI_ADPTSH(adapterTSH)->OSDBuildTargetSCB = OSDStandardU320BuildTargetSCB;
#endif /* SCSI_TARGET_OPERATION */
         SCSI_ENABLE_DAC(adapterTSH);
#if (OSM_BUS_ADDRESS_SIZE == 32)
         SCSI_ADPTSH(adapterTSH)->allocBusAddressSize = 32; 
#else         
         SCSI_ADPTSH(adapterTSH)->allocBusAddressSize = 64; 
#endif /* (OSM_BUS_ADDRESS_SIZE == 32) */
      }
      else
#endif /* SCSI_STANDARD_U320_MODE */
#if SCSI_STANDARD_ENHANCED_U320_MODE
      if (fwMode == (SCSI_UEXACT8)SCSI_FMODE_STANDARD_ENHANCED_U320)
      {
         /* Standard Ultra 320 mode */
         hhcb->firmwareMode = SCSI_FMODE_STANDARD_ENHANCED_U320;
         SCSI_ADPTSH(adapterTSH)->OSDBuildSCB = OSDStandardEnhU320BuildSCB;
#if SCSI_TARGET_OPERATION
         /* setup build SCB routine function pointer for target operation */
         SCSI_ADPTSH(adapterTSH)->OSDBuildTargetSCB = OSDStandardEnhU320BuildTargetSCB;
#endif /* SCSI_TARGET_OPERATION */
         SCSI_ENABLE_DAC(adapterTSH);
#if (OSM_BUS_ADDRESS_SIZE == 32)
         SCSI_ADPTSH(adapterTSH)->allocBusAddressSize = 32; 
#else         
         SCSI_ADPTSH(adapterTSH)->allocBusAddressSize = 64; 
#endif /* (OSM_BUS_ADDRESS_SIZE == 32) */
      }
      else
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */
      {
         OSMDebugPrint(1,("SCSIVerifyAdapter %d", (SCSI_UEXACT8)7));
         return((HIM_UINT8)HIM_ADAPTER_NOT_SUPPORTED);
      }
         
      for (i = 0; i < SCSI_MAXDEV; i++)
      {
         SCSI_ADPTSH(adapterTSH)->NumberLuns[i] = SCSI_MAXLUN;
      }

#if SCSI_PAC_SEND_SSU
      SCSI_ADPTSH(adapterTSH)->SCSI_AF_ssuInProgress = 0;
#endif /* SCSI_PAC_SEND_SSU */

#if SCSI_DOMAIN_VALIDATION
      if ((hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320) ||
          (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320))
      {
#if SCSI_DOMAIN_VALIDATION_ENHANCED
         hhcb->domainValidationMethod = SCSI_DV_ENHANCED;
#else /* SCSI_DOMAIN_VALIDATION_ENHANCED */
         hhcb->domainValidationMethod = SCSI_DV_BASIC;
#endif /* SCSI_DOMAIN_VALIDATION_ENHANCED */
         SCSI_ADPTSH(adapterTSH)->domainValidationIDMask = 0xFFFF; /* all IDs enabled */
      }
      else
      {
         hhcb->domainValidationMethod = SCSI_DV_DISABLE;
         SCSI_ADPTSH(adapterTSH)->domainValidationIDMask = 0; /* all IDs disabled */
      } 
#endif /* SCSI_DOMAIN_VALIDATION */

      /* get current configuration */
      hhcb->osdHandle = SCSI_ADPTSH(adapterTSH)->osmAdapterContext;
      numberScbs = hhcb->numberScbs;
      SCSI_GET_CONFIGURATION(hhcb);

      /* get bios information */
#if SCSI_BIOS_ASPI8DOS
      SCSIxGetBiosInformation(adapterTSH);
#endif /* SCSI_BIOS_ASPI8DOS */
   
      /* update the hhcb per nvram configuration */
      SCSI_APPLY_NVDATA(hhcb);

      /* reconfigure the hhcb based on the previous hardware state */
      SCSI_UPDATE_DEVICE_TABLE(hhcb);

      /* Set AP_InitializeIOBus indicator in adapter TSCB */ 
      SCSI_ADPTSH(adapterTSH)->initializeIOBus = 
         (hhcb->SCSI_HF_resetSCSI == 1) ? HIM_TRUE : HIM_FALSE;

      /* numberScbs must be less than the value OSM configured */
      if (hhcb->numberScbs > numberScbs)
      {
         hhcb->numberScbs = numberScbs;
     
         /* need to recalculate number of est scbs */
         SCSI_TARGET_GETCONFIG(hhcb);

#if SCSI_TARGET_OPERATION
         if (hhcb->SCSI_HF_targetMode && 
             ((hhcb->SCSI_HF_targetNumberEstScbs == 0) ||
              (hhcb->SCSI_HF_targetNumberEstScbs >= (hhcb->numberScbs - 3))))
         {
            /* Make sure we have a valid number of establish connection SCBs */
            /* We need some scbs for initiator mode and/or reestablish SCBs */
            /* Note; at some point this test may become firmware mode specific */   
            status = HIM_ADAPTER_NOT_SUPPORTED;
         }
#endif /* SCSI_TARGET_OPERATION */    
      }
   }
   else
   {
      status = HIM_ADAPTER_NOT_SUPPORTED;
   }

#else

   /*************************************************** 
   Need to add the reading of Rev-Device type info from 
   the DCH regs in the routine SCSIxDetermineFirmwareMode. 
    ****************************************************/ 

   /* setup hhcb before it's ready to be used */
   hhcb->scsiRegister = &SCSI_ADPTSH(adapterTSH)->scsiHandle;

   fwMode = SCSIxDetermineFirmwareMode(SCSI_ADPTSH(adapterTSH));
   
   if (fwMode == (SCSI_UEXACT8)SCSI_FMODE_DCH_U320)
   {
         /* DCH 320 mode */
      hhcb->firmwareMode = SCSI_FMODE_DCH_U320;
      SCSI_ADPTSH(adapterTSH)->OSDBuildSCB = OSDDchU320BuildSCB;
#if SCSI_TARGET_OPERATION
      /* setup build SCB routine function pointer for target operation */
      SCSI_ADPTSH(adapterTSH)->OSDBuildTargetSCB = OSDDchU320BuildTargetSCB;
#endif /* SCSI_TARGET_OPERATION */
      /*  SCSI_ENABLE_DAC(adapterTSH); */
      SCSI_ADPTSH(adapterTSH)->allocBusAddressSize = 64; 
   }
   else
   {
      OSDDebugPrint(1,("SCSIVerifyAdapter %d", (SCSI_UEXACT8)7));
      return((HIM_UINT8)HIM_ADAPTER_NOT_SUPPORTED);
   }
         
   for (i = 0; i < SCSI_MAXDEV; i++)
   {
      SCSI_ADPTSH(adapterTSH)->NumberLuns[i] = SCSI_MAXLUN;
   }

#if SCSI_PAC_SEND_SSU
   SCSI_ADPTSH(adapterTSH)->SCSI_AF_ssuInProgress = 0;
#endif /* SCSI_PAC_SEND_SSU */

#if SCSI_DOMAIN_VALIDATION
   if (hhcb->firmwareMode == SCSI_FMODE_DCH_U320)
   {
#if SCSI_DOMAIN_VALIDATION_ENHANCED
      hhcb->domainValidationMethod = SCSI_DV_ENHANCED;
#else /* SCSI_DOMAIN_VALIDATION_ENHANCED */
      hhcb->domainValidationMethod = SCSI_DV_BASIC;
#endif /* SCSI_DOMAIN_VALIDATION_ENHANCED */
      SCSI_ADPTSH(adapterTSH)->domainValidationIDMask = 0xFFFF; /* all IDs enabled */
   }
   else
   {
      hhcb->domainValidationMethod = SCSI_DV_DISABLE;
      SCSI_ADPTSH(adapterTSH)->domainValidationIDMask = 0; /* all IDs disabled */
   } 
#endif /* SCSI_DOMAIN_VALIDATION */

      /* get current configuration */
   hhcb->osdHandle = SCSI_ADPTSH(adapterTSH)->osmAdapterContext;
   numberScbs = hhcb->numberScbs;
   SCSI_GET_CONFIGURATION(hhcb);

   /* update the hhcb per nvram configuration */
   SCSI_APPLY_NVDATA(hhcb);

   /* reconfigure the hhcb based on the previous hardware state */
   SCSI_UPDATE_DEVICE_TABLE(hhcb);

   /* Set AP_InitializeIOBus indicator in adapter TSCB */ 
   SCSI_ADPTSH(adapterTSH)->initializeIOBus =  HIM_TRUE;
   /* (hhcb->SCSI_HF_resetSCSI == 1) ? HIM_TRUE : HIM_FALSE; */

   /* numberScbs must be less than the value OSM configured */
   if (hhcb->numberScbs > numberScbs)
   {
      hhcb->numberScbs = numberScbs;
  
      /* need to recalculate number of est scbs */
      SCSI_TARGET_GETCONFIG(hhcb); 
#if SCSI_TARGET_OPERATION
      if (hhcb->SCSI_HF_targetMode && 
          ((hhcb->SCSI_HF_targetNumberEstScbs == 0) ||
           (hhcb->SCSI_HF_targetNumberEstScbs >= (hhcb->numberScbs - 3))))
      {
         /* Make sure we have a valid number of establish connection SCBs */
         /* We need some scbs for initiator mode and/or reestablish SCBs */
         /* Note; at some point this test may become firmware mode specific */   
         status = HIM_ADAPTER_NOT_SUPPORTED;
      }
#endif /* SCSI_TARGET_OPERATION */    
   }

#endif /* !SCSI_DCH_U320_MODE */
#if SCSI_TARGET_OPERATION
   /* Initialize hhcb fields. Profiling can now occur.                   */
   /* Note that; initiatorMode, targetMode, targetNumNodeTaskSetHandles, */
   /* and targetNumNexusTaskSetHandles                                   */
   /* were initialized in SCSICreateAdapterTSCB.                         */                     
   
   hhcb->SCSI_HF_targetGroup6CDBsz = SCSI_Group6CDBDefaultSize;
   hhcb->SCSI_HF_targetGroup7CDBsz = SCSI_Group7CDBDefaultSize;
   hhcb->SCSI_HF_targetHostTargetVersion = HIM_SCSI_2;
   hhcb->SCSI_HF_targetDisconnectAllowed = 1;
   hhcb->SCSI_HF_targetTagEnable = 1;
   hhcb->SCSI_HF_targetOutOfOrderTransfers = 1;
   hhcb->SCSI_HF_targetScsi2IdentifyMsgRsv = 1; 
   hhcb->SCSI_HF_targetScsi2RejectLuntar = 0;
   hhcb->SCSI_HF_targetInitNegotiation = 0;
   hhcb->SCSI_HF_targetIgnoreWideResidMsg = 1;
   hhcb->SCSI_HF_targetEnableScsi1Selection = 0;
   hhcb->SCSI_HF_targetNexusThreshold = 0;
   hhcb->SCSI_HF_targetHiobQueueThreshold = 0;      
   hhcb->SCSI_HF_targetTaskMngtOpts = 0;
   hhcb->SCSI_HF_targetAbortTask = 1;
   hhcb->SCSI_HF_targetClearTaskSet = 1;
   hhcb->SCSI_HF_targetAdapterIDMask =
      (SCSI_UEXACT16) (1 << hhcb->hostScsiID);  

#endif /* SCSI_TARGET_OPERATION */

   OSMDebugPrint(1,("SCSIVerifyAdapter %d", (SCSI_UEXACT8)8));
   return(status);
}

/*********************************************************************
*
*  SCSIxEnableDAC
*
*     Enable dual address cycle
*
*  Return Value:  none
*                  
*  Parameters:    adapter TSCB
*
*  Remarks:       This routine should be referenced only if 64 bits
*                 addressing has been enabled through OSM_DMA_SWAP_WIDTH
*                 defined as 64.
*
*********************************************************************/
#if (OSM_BUS_ADDRESS_SIZE == 64)
void SCSIxEnableDAC (HIM_TASK_SET_HANDLE adapterTSH)
{
   HIM_TASK_SET_HANDLE osmAdapterContext =
                           SCSI_ADPTSH(adapterTSH)->osmAdapterContext;
   SCSI_UEXACT32 uexact32Value;

   if (!((uexact32Value = OSMxReadPCIConfigurationDword(osmAdapterContext,
                                          SCSI_DEVCONFIG0_REG)) & SCSI_DACEN))
   {
      OSMxWritePCIConfigurationDword(osmAdapterContext,SCSI_DEVCONFIG0_REG,
                                          uexact32Value | SCSI_DACEN);
   }
}
#endif /* (OSM_BUS_ADDRESS_SIZE == 64) */

/*********************************************************************
*
*  SCSIInitialize
*
*     Initialize specified host device (hardware)
*
*  Return Value:  HIM_SUCCESS, initialization successfully
*                 HIM_FAILURE, adapter initialization failure
*                  
*  Parameters:    adapter TSCB
*
*  Remarks:       This routine can call SCSIrGetConfiguration (if it's 
*                 not been called yet) and SCSIrInitialize to initialize 
*                 host adapter. Protocol automatic configuration 
*                 procedure must be executed before normal io request
*                 can be executes. SCSIQueueIOB can be invoked to
*                 execute protocol automatic configuration.
*
*********************************************************************/
HIM_UINT8 SCSIInitialize (HIM_TASK_SET_HANDLE adapterTSH)
{
   SCSI_HHCB HIM_PTR hhcb = &SCSI_ADPTSH(adapterTSH)->hhcb;
   HIM_UINT8 returnValue;
#if SCSI_DOMAIN_VALIDATION
   SCSI_UEXACT8 i;
#endif /* SCSI_DOMAIN_VALIDATION */
#if SCSI_ACCESS_RAM
   SCSI_UEXACT8 databuffer[4] = "CHIM";
#endif /* SCSI_ACCESS_RAM */

   /* apply memory table to internal HIM */
   /* numberScbs may get adjusted with following call as well */
   if (SCSI_APPLY_MEMORY_TABLE(hhcb,&SCSI_ADPTSH(adapterTSH)->memoryTable) ==
                                                                  SCSI_FAILURE)
   {
      return((HIM_UINT8)(HIM_FAILURE));
   }

#if SCSI_DOMAIN_VALIDATION
   /* clear out Throttle down for OSM bus reset */
   for (i = 0; i < hhcb->maxDevices; i++)
   {
      SCSI_ADPTSH(adapterTSH)->SCSI_AF_dvThrottleOSMReset(i) = 0;
   }
#endif /* SCSI_DOMAIN_VALIDATION */

   /* initialize the host adapter */
   returnValue = SCSI_INITIALIZE(hhcb);

   if (returnValue == SCSI_SUCCESS)
   {
      returnValue = HIM_SUCCESS;
      /* set flag for initialized */
      SCSI_ADPTSH(adapterTSH)->SCSI_AF_initialized = 1;
   }   
   else
   {
      returnValue = HIM_FAILURE;
   }   

#if SCSI_ACCESS_RAM
   /* Load scb ptr with 0xFF */
   SCSI_WRITE_SCB_RAM(hhcb,
                      (SCSI_UEXACT16)SCSI_U320_SCBFF_PTR,
                      (SCSI_UEXACT8) 0,
                      (SCSI_UEXACT8) 4,
                      (SCSI_UEXACT8 HIM_PTR) &databuffer[0]);
#endif /* SCSI_ACCESS_RAM */
   return(returnValue);
}

/*********************************************************************
*
*  SCSISizeTargetTSCB
*
*     Get target TSCB size
*
*  Return Value:  target TSCB size in bytes 
*                  
*  Parameters:    adapter TSCB
*
*  Remarks:       As far as SCSI CHIM concern the associated target
*                 TSCB size can be found with sizeof(SCSI_DEVICE_CONTROL).
*                 CHIM layer does not have to know how SCSI_DEVICE_CONTROL
*                 get structured. CHIM layer may add other memory 
*                 required into size for its own convenience.
*
*********************************************************************/
HIM_UINT32 SCSISizeTargetTSCB (HIM_TASK_SET_HANDLE adapterTSH)
{
   return(sizeof(SCSI_TARGET_TSCB));
}

/*********************************************************************
*
*  SCSICheckTargetTSCBNeeded
*
*     Check target TSCB needed
*
*  Return Value:  HIM_NO_NEW_DEVICES, no more new TSCB needed
*                 HIM_NEW_DEVICE_DETECTED, new target device detected
*                  
*  Parameters:    adapter TSH
*                 index
*
*  Remarks:       The return value from this routine will tell OSM if
*                 there is new target device encountered by CHIM. New 
*                 target TSCB should be allocated for each target device
*                 found. SCSICreateTargetTSCB should be called to
*                 make new TSCB availabe for CHIM to use.
*
*********************************************************************/
HIM_UINT8 SCSICheckTargetTSCBNeeded (HIM_TASK_SET_HANDLE adapterTSH,
                                     HIM_UINT16 index)
{
#if SCSI_INITIATOR_OPERATION
   SCSI_UEXACT8 HIM_PTR lunExist = SCSI_ADPTSH(adapterTSH)->lunExist;
   SCSI_UEXACT8 HIM_PTR tshExist = SCSI_ADPTSH(adapterTSH)->tshExist;
   HIM_UINT8 i;
   HIM_UINT8 j;
   SCSI_HHCB HIM_PTR hhcb = (SCSI_HHCB HIM_PTR) &SCSI_ADPTSH(adapterTSH)->hhcb;

   if (!hhcb->SCSI_HF_initiatorMode)
   {
      return((HIM_UINT8)(HIM_NO_NEW_DEVICES));
   }

#if !SCSI_DISABLE_PROBE_SUPPORT
   if (index == HIM_PROBED_TARGET)
   {
      i = SCSI_ADPTSH(adapterTSH)->lastScsiIDProbed;
      j = SCSI_ADPTSH(adapterTSH)->lastScsiLUNProbed;
      if (SCSIxChkLunExist(lunExist,i,j) != SCSIxChkLunExist(tshExist,i,j) && 
          SCSIxChkLunExist(lunExist,i,j) == 1)
      {
         /* lun exist but no task set handle available yet */
         return((HIM_UINT8)(HIM_NEW_DEVICE_DETECTED));
      }
   }
   else
#endif /* !SCSI_DISABLE_PROBE_SUPPORT */
   {
      for (i = 0; i < SCSI_MAXDEV; i++)
      {
         for (j = 0; j < SCSI_ADPTSH(adapterTSH)->NumberLuns[i]; j++)
         {
            if (SCSIxChkLunExist(lunExist,i,j) != SCSIxChkLunExist(tshExist,i,j) && 
                SCSIxChkLunExist(lunExist,i,j) == 1)
            {
               /* lun exist but no task set handle available yet */
               return((HIM_UINT8)(HIM_NEW_DEVICE_DETECTED));
            }
         }
      }
   }

#endif /* SCSI_INITIATOR_OPERATION */
   return((HIM_UINT8)(HIM_NO_NEW_DEVICES));
}

/*********************************************************************
*
*  SCSICreateTargetTSCB
*
*     Check target TSCB needed
*
*  Return Value:  target task set handle
*                 0, no more new TSCB needed
*                 
*                  
*  Parameters:    target TSH
*                 index
*                 target TSCB
*                 
*
*  Remarks:       The returned target TSH will be available for OSM
*                 to send request to associated target device.
*
*********************************************************************/
HIM_TASK_SET_HANDLE SCSICreateTargetTSCB (HIM_TASK_SET_HANDLE adapterTSH,
                                          HIM_UINT16 index,
                                          void HIM_PTR targetTSCB)
{
#if SCSI_INITIATOR_OPERATION
   SCSI_HHCB HIM_PTR hhcb = (SCSI_HHCB HIM_PTR) &SCSI_ADPTSH(adapterTSH)->hhcb;
   SCSI_UEXACT8 HIM_PTR lunExist = SCSI_ADPTSH(adapterTSH)->lunExist;
   SCSI_UEXACT8 HIM_PTR tshExist = SCSI_ADPTSH(adapterTSH)->tshExist;
   HIM_UEXACT8 i;
   HIM_UEXACT8 j;

   if (!hhcb->SCSI_HF_initiatorMode)
   {
      return(SCSI_TRGTSH(0));
   }

#if !SCSI_DISABLE_PROBE_SUPPORT
   if (index == HIM_PROBED_TARGET)
   {
      i = (HIM_UEXACT8) SCSI_ADPTSH(adapterTSH)->lastScsiIDProbed;
      j = (HIM_UEXACT8) SCSI_ADPTSH(adapterTSH)->lastScsiLUNProbed;
      if (SCSIxChkLunExist(lunExist,i,j) != SCSIxChkLunExist(tshExist,i,j) && 
          SCSIxChkLunExist(lunExist,i,j) == 1)
      {
         /* lun exist but no task set handle available yet */
         SCSIxSetLunExist(tshExist,i,j);

         /* assign scsi id and lun id */
         SCSI_TRGTSH(targetTSCB)->scsiID = i;
         SCSI_TRGTSH(targetTSCB)->lunID = j;
         SCSI_TRGTSH(targetTSCB)->adapterTSH = SCSI_ADPTSH(adapterTSH);

         SCSI_TRGTSH(targetTSCB)->targetAttributes.tagEnable =
            (((SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DF_disconnectEnable << i) &
              SCSI_ADPTSH(adapterTSH)->tagEnable) != 0);

         /* setup target's TSCB type */
         SCSI_TRGTSH(targetTSCB)->typeTSCB = SCSI_TSCB_TYPE_TARGET;

         /* validate unit control */
         SCSI_SET_UNIT_HANDLE(hhcb,&SCSI_TRGTSH(targetTSCB)->unitControl,i,j);
            
         return(SCSI_TRGTSH(targetTSCB));
      }
   }
   else
#endif /* !SCSI_DISABLE_PROBE_SUPPORT */
   {
      for (i = 0; i < SCSI_MAXDEV; i++)
      {
         for (j = 0; j < SCSI_ADPTSH(adapterTSH)->NumberLuns[i]; j++)
         {
            if (SCSIxChkLunExist(lunExist,i,j) != SCSIxChkLunExist(tshExist,i,j) && 
                SCSIxChkLunExist(lunExist,i,j) == 1)
            {
               /* lun exist but no task set handle available yet */
               SCSIxSetLunExist(tshExist,i,j);

               /* assign scsi id and lun id */
               SCSI_TRGTSH(targetTSCB)->scsiID = i;
               SCSI_TRGTSH(targetTSCB)->lunID = j;
               SCSI_TRGTSH(targetTSCB)->adapterTSH = SCSI_ADPTSH(adapterTSH);

               SCSI_TRGTSH(targetTSCB)->targetAttributes.tagEnable =
                  (((SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DF_disconnectEnable << i) &
                    SCSI_ADPTSH(adapterTSH)->tagEnable) != 0);

               /* setup target's TSCB type */
               SCSI_TRGTSH(targetTSCB)->typeTSCB = SCSI_TSCB_TYPE_TARGET;

               /* validate unit control */
               SCSI_SET_UNIT_HANDLE(hhcb,&SCSI_TRGTSH(targetTSCB)->unitControl,i,j);

               return(SCSI_TRGTSH(targetTSCB));
            }
         }
      }
   }

#endif /* SCSI_INITIATOR_OPERATION */

   return(SCSI_TRGTSH(0));
}






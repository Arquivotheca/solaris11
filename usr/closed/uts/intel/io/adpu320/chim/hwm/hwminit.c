/*$Header: /vobs/u320chim/src/chim/hwm/hwminit.c   /main/174   Fri Aug 22 16:18:48 2003   hu11135 $*/

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
*  Module Name:   HWMINIT.C
*
*  Description:
*                 Codes to initialize hardware management module
*
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         NONE
*
*  Entry Point(s):
*                 SCSIHGetConfiguration
*                 SCSIHGetMemoryTable
*                 SCSIHApplyMemoryTable
*                 SCSIHInitialize
*                 SCSIHSetupHardware
*
***************************************************************************/

#define  SCSI_DATA_DEFINE

#include "scsi.h"
#include "hwm.h"

/*********************************************************************
*
*  SCSIHGetConfiguration
*
*     Get default configuration information
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine requires scsiRegister valid in
*                 order to access the hardware. This routine
*                 may also access pci configuration space in 
*                 order to collect configuration information.
*
*********************************************************************/
void SCSIHGetConfiguration (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 savedMode;

   /* We need to turn off the Power Down mode before we can access to */
   /* any chip's registers.  At the same time, we have to clear the   */
   /* CHIPRST bit as well.  Therefore, we save the CHIPRST state in   */
   /* our HHCB for later reference. */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));
/* Check reset status for poweron or for any block reset occurrences */
#if SCSI_DCH_U320_MODE
 
   if (hcntrl & SCSI_DCHRST)
   {
      hhcb->SCSI_HF_chipReset = 1;
      /* clear the hst_reset status reg.. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_HCNTRL),(hcntrl & ~SCSI_DCHRST)); 
   }
#endif /* SCSI_DCH_U320_MODE */   

#if !SCSI_DCH_U320_MODE
   if (!hhcb->SCSI_HF_chipReset)
   {
      hhcb->SCSI_HF_chipReset = hcntrl & SCSI_CHIPRST;
   }

   /* Turn off Power Down mode and clear CHIPRST bit if and only if */
   /* the chip is in Power Down mode. */
   if (hcntrl & SCSI_POWRDN)
   {
      hcntrl &= ~(SCSI_POWRDN | SCSI_CHIPRST);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_HCNTRL), hcntrl);
   }
#else      
   if (!hhcb->SCSI_HF_chipReset)
   {
      hhcb->SCSI_HF_chipReset = hcntrl & SCSI_DCHRST;
   }
   /* Turn off Power Down mode and clear CHIPRST bit if and only if */
   /* the chip is in Power Down mode. */
   if (hcntrl & SCSI_POWRDN)
   {
      hcntrl &= ~(SCSI_POWRDN | SCSI_DCHRST);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_HCNTRL), hcntrl);
   }
#endif /* !SCSI_DCH_U320_MODE */


   /* Set appropriate function pointers depending on mode of operation */
   /* the mode of operation is combination of firmware mode and compilation */
   /* mode (BIOS does not reference function pointer to save space) */
   SCSIhSetupEnvironment(hhcb);

   /* Get hardware revision level and firmware (e.g. sequencer) */
   /* version number */
   /* check if chip reset bit asserted. psuse chip if reset was cleared */
   /* set initialization required flag if it was set */
   /* check if pause chip necessary */

   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);      
   }

   /* Get current MODE_PTR value before any potential
    * register accesses.
    */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   SCSIhPrepareConfig(hhcb);

   /* Get configuration information which is dependent on firmware */
   /* mode of operation */
   SCSI_hGETCONFIGURATION(hhcb);

   /* Get current hardware configuration */
   SCSIhGetHardwareConfiguration(hhcb);

   /* Restore mode before unpausing sequencer */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);

   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }

   OSD_SYNCHRONIZE_IOS(hhcb);

}

/*********************************************************************
*
*  SCSIHGetMemoryTable
*
*     This routine will collect memory requirement information and 
*     fill the memory table.
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 memoryTable
*
*  Remarks:       This routine may get called before/after 
*                 SCSIHGetConfiguration. It requires firmwareMode set
*                 with intended operating mode before calling
*                 this routine.
*
*                 This routine should not assume harwdare can be
*                 accessed. (e.g. scsiRegister may not be valid
*                 when this routine get called.
*                 
*********************************************************************/
#if !SCSI_BIOS_SUPPORT        
void SCSIHGetMemoryTable (SCSI_UEXACT8 firmwareMode,
                          SCSI_UEXACT16 numberScbs,
                          SCSI_MEMORY_TABLE SCSI_HPTR memoryTable)
{
#if !SCSI_DOWNSHIFT_MODE
   SCSI_UEXACT16 tnScbs;
#endif /* !SCSI_DOWNSHIFT_MODE */ 

   /* Active HIOB array (indexed by SCB number) memory characteristics. */ 
   /* Assign memory type. */                
   memoryTable->memory[SCSI_HM_ACTIVEPTRARRAY].memoryType = SCSI_MT_HPTR;
   /* Assign memory category. */
   memoryTable->memory[SCSI_HM_ACTIVEPTRARRAY].memoryCategory = SCSI_MC_UNLOCKED;
   /* Assign alignment. */
   memoryTable->memory[SCSI_HM_ACTIVEPTRARRAY].memoryAlignment =
      (SCSI_UEXACT8)(sizeof(SCSI_HIOB SCSI_IPTR)-1);
   /* Assign granularity. */                
   memoryTable->memory[SCSI_HM_ACTIVEPTRARRAY].granularity = 0;
   /* Assign size. */
   memoryTable->memory[SCSI_HM_ACTIVEPTRARRAY].memorySize = 
      sizeof(SCSI_HIOB SCSI_IPTR) * numberScbs;
   /* Minimum size. */
   memoryTable->memory[SCSI_HM_ACTIVEPTRARRAY].minimumSize =
      memoryTable->memory[SCSI_HM_ACTIVEPTRARRAY].memorySize;

#if SCSI_SCBBFR_BUILTIN
   /* SCB buffer memory characteristics. */
   /* Assign memory type. */
   memoryTable->memory[SCSI_HM_SCBBFRARRAY].memoryType = SCSI_MT_IPTR;

   /* Assign memory category. */
   memoryTable->memory[SCSI_HM_SCBBFRARRAY].memoryCategory = SCSI_MC_LOCKED;
   /* Assign alignment. */
   memoryTable->memory[SCSI_HM_SCBBFRARRAY].memoryAlignment = 
      (SCSI_UEXACT8)SCSI_hALIGNMENTSCBBUFFER(firmwareMode);
   /* Assign granularity. */                
   memoryTable->memory[SCSI_HM_SCBBFRARRAY].granularity =
      (SCSI_UEXACT8) SCSI_hSIZE_OF_SCB_BUFFER(firmwareMode);
   /* Assign size. */
   memoryTable->memory[SCSI_HM_SCBBFRARRAY].memorySize = 
      numberScbs * SCSI_hSIZE_OF_SCB_BUFFER(firmwareMode);
   /* Minimum size. */
   memoryTable->memory[SCSI_HM_SCBBFRARRAY].minimumSize = (SCSI_UEXACT16)
      (SCSI_MINSCBS * SCSI_hSIZE_OF_SCB_BUFFER(firmwareMode));

   /* SCB buffer descriptor memory characteristics. */ 
   /* Assign memory type. */                
   memoryTable->memory[SCSI_HM_FREEQUEUE].memoryType = SCSI_MT_HPTR;
   /* Assign memory category. */
   memoryTable->memory[SCSI_HM_FREEQUEUE].memoryCategory = SCSI_MC_UNLOCKED;
   /* Assign alignment. */
   memoryTable->memory[SCSI_HM_FREEQUEUE].memoryAlignment =
      (SCSI_UEXACT8)0x03;
   /* Assign granularity. */                
   memoryTable->memory[SCSI_HM_FREEQUEUE].granularity = 0;
   /* Assign size. */
   memoryTable->memory[SCSI_HM_FREEQUEUE].memorySize =
      sizeof(SCSI_SCB_DESCRIPTOR) * numberScbs;
   /* Minimum size. */
   memoryTable->memory[SCSI_HM_FREEQUEUE].minimumSize = 
      memoryTable->memory[SCSI_HM_FREEQUEUE].memorySize;
#endif /* SCSI_SCBBFR_BUILTIN */

#if !SCSI_DOWNSHIFT_MODE
   /* Sequencer completion queue memory characteristics. */
   /* Assign memory type. */                
   memoryTable->memory[SCSI_HM_QOUTPTRARRAY].memoryType = SCSI_MT_HPTR;
   /* Assign memory category. */
   memoryTable->memory[SCSI_HM_QOUTPTRARRAY].memoryCategory = SCSI_MC_LOCKED;
   /* Assign alignment. */
   memoryTable->memory[SCSI_HM_QOUTPTRARRAY].memoryAlignment = (SCSI_UEXACT8) 0xFF;
   /* Assign granularity. */                
   memoryTable->memory[SCSI_HM_QOUTPTRARRAY].granularity = 0;

   tnScbs = SCSI_MINDONEQELEMENTS;
   while (tnScbs < numberScbs)
   {
      tnScbs *= 2;
   }

   /* Assign size. */
   memoryTable->memory[SCSI_HM_QOUTPTRARRAY].memorySize = 
      sizeof(SCSI_QOUTFIFO_NEW) * tnScbs;
   /* Minimum size. */
   memoryTable->memory[SCSI_HM_QOUTPTRARRAY].minimumSize = 
      memoryTable->memory[SCSI_HM_QOUTPTRARRAY].memorySize; 

#if SCSI_PACKETIZED_IO_SUPPORT
   /* SPI status packet buffer memory characteristics. */
   /* Assign memory type. */                
   memoryTable->memory[SCSI_HM_SPISTATUS].memoryType = SCSI_MT_HPTR;
   /* Assign memory category. */
   memoryTable->memory[SCSI_HM_SPISTATUS].memoryCategory = SCSI_MC_LOCKED;
   /* Assign alignment. */
   memoryTable->memory[SCSI_HM_SPISTATUS].memoryAlignment = 0x03;
   /* Assign granularity. */                
   memoryTable->memory[SCSI_HM_SPISTATUS].granularity = 0;

   /* Assign size. */
#if SCSI_INITIATOR_OPERATION
   memoryTable->memory[SCSI_HM_SPISTATUS].memorySize =
      (SCSI_UEXACT16) SCSI_MAX_STATUS_IU_SIZE * 2;
   /* Minimum size. */
   memoryTable->memory[SCSI_HM_SPISTATUS].minimumSize =
      (SCSI_UEXACT16) SCSI_MAX_STATUS_IU_SIZE * 2;
#else
   /* Adjust memory size to a minimum as this memory will
    * not be used. Unfortunately, we need to set this to a 
    * non-zero value when requesting memory.
    */
   memoryTable->memory[SCSI_HM_SPISTATUS].memorySize =
      (SCSI_UEXACT16) sizeof(SCSI_BUS_ADDRESS);
   /* Minimum size. */
   memoryTable->memory[SCSI_HM_SPISTATUS].minimumSize =
      (SCSI_UEXACT16) sizeof(SCSI_BUS_ADDRESS);
#endif /* SCSI_INITIATOR_OPERATION */

#if (SCSI_TARGET_OPERATION && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
   /* Target Mode SPI status packet buffer memory characteristics. */
   /* Assign memory type. */                
   memoryTable->memory[SCSI_HM_TM_SPISTATUS].memoryType = SCSI_MT_HPTR;
   /* Assign memory category. */
   memoryTable->memory[SCSI_HM_TM_SPISTATUS].memoryCategory = SCSI_MC_LOCKED;
   /* Assign alignment. */
   memoryTable->memory[SCSI_HM_TM_SPISTATUS].memoryAlignment =
      sizeof(SCSI_BUS_ADDRESS)-1;
   /* Assign granularity. */                
   memoryTable->memory[SCSI_HM_TM_SPISTATUS].granularity = 0;
   /* Assign size. */
   memoryTable->memory[SCSI_HM_TM_SPISTATUS].memorySize =
      (SCSI_UEXACT16) ((SCSI_TM_SENSE_IU_SIZE * SCSI_TM_SENSE_IU_COUNT) +
                       (SCSI_TM_PKT_FAILURE_IU_SIZE *
                        SCSI_TM_PKT_FAILURE_IU_COUNT));
   /* Minimum size. */
   memoryTable->memory[SCSI_HM_TM_SPISTATUS].minimumSize =
      memoryTable->memory[SCSI_HM_TM_SPISTATUS].memorySize;

   /* Packetized Target Mode Error Handling - unlocked memory. */
   /* Assign memory type. */                
   memoryTable->memory[SCSI_HM_TM_ERROR_RECOVERY].memoryType = SCSI_MT_HPTR;
   /* Assign memory category. */
   memoryTable->memory[SCSI_HM_TM_ERROR_RECOVERY].memoryCategory = SCSI_MC_UNLOCKED;
   /* Assign alignment. */
   memoryTable->memory[SCSI_HM_TM_ERROR_RECOVERY].memoryAlignment =
      sizeof(SCSI_BUS_ADDRESS)-1;
   /* Assign granularity. */                
   memoryTable->memory[SCSI_HM_TM_ERROR_RECOVERY].granularity = 0;
   /* Assign size. */
   /* Note that COVQ is required beacuse some OS's require pointers on 
    * boundaries.
    */
   memoryTable->memory[SCSI_HM_TM_ERROR_RECOVERY].memorySize =
      (SCSI_UEXACT16) (SCSI_COVQ(sizeof(SCSI_HIOB),sizeof(void SCSI_IPTR)) +
                       SCSI_COVQ(sizeof(SCSI_NEXUS),sizeof(void SCSI_XPTR)));
   /* Minimum size. */
   memoryTable->memory[SCSI_HM_TM_ERROR_RECOVERY].minimumSize =
      memoryTable->memory[SCSI_HM_TM_ERROR_RECOVERY].memorySize;
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

#endif /* !SCSI_DOWNSHIFT_MODE */

}
#endif /* !SCSI_BIOS_SUPPORT */

/*********************************************************************
*
*  SCSIHApplyMemoryTable
*
*     This routine will apply memory pointers described in memory table.
*
*  Return Value:  SCSI_SUCCESS - memory table applied
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
#if !SCSI_BIOS_SUPPORT
SCSI_UINT8 SCSIHApplyMemoryTable (SCSI_HHCB SCSI_HPTR hhcb,
                                  SCSI_MEMORY_TABLE SCSI_HPTR memoryTable)
{
#if SCSI_SCBBFR_BUILTIN
   SCSI_UEXACT16 numberScbs;
   SCSI_UEXACT8 SCSI_HPTR virtualAddress;
   SCSI_BUS_ADDRESS busAddress;
   SCSI_UEXACT16 i;
#endif /* SCSI_SCBBFR_BUILTIN */
#if !SCSI_DOWNSHIFT_MODE  
   SCSI_UEXACT16 totalDoneQElements;
#endif /* !SCSI_DOWNSHIFT_MODE */  
   SCSI_UINT8 status = SCSI_SUCCESS;

   /* Set memory pointer for active pointer array. */
   hhcb->SCSI_ACTIVE_PTR_ARRAY = (SCSI_HIOB SCSI_IPTR SCSI_HPTR)
            memoryTable->memory[SCSI_HM_ACTIVEPTRARRAY].ptr.hPtr;

#if SCSI_SCBBFR_BUILTIN
   /* Set memory pointer for SCB buffer memory. */
   hhcb->SCSI_SCB_BUFFER.virtualAddress = (void SCSI_HPTR)
            memoryTable->memory[SCSI_HM_SCBBFRARRAY].ptr.hPtr;

   numberScbs =
      (SCSI_UEXACT16)(memoryTable->memory[SCSI_HM_SCBBFRARRAY].memorySize /
      SCSI_hSIZE_OF_SCB_BUFFER(hhcb->firmwareMode));

   if (numberScbs >= SCSI_MINSCBS)
   {
      hhcb->numberScbs = numberScbs;
   }
   else
   {
      status = SCSI_FAILURE;
   }

   /* Set memory pointer for free scb descriptors queue. */
   hhcb->SCSI_FREE_QUEUE = (SCSI_SCB_DESCRIPTOR SCSI_HPTR)
                        memoryTable->memory[SCSI_HM_FREEQUEUE].ptr.hPtr;

   /* Build the linked list of SCB buffers */
   hhcb->SCSI_FREE_SCB_AVAIL = hhcb->numberScbs;
   hhcb->SCSI_FREE_HEAD = &hhcb->SCSI_FREE_QUEUE[0];
   hhcb->SCSI_FREE_TAIL = &hhcb->SCSI_FREE_QUEUE[hhcb->numberScbs - 1];

   for (i = 0; i < hhcb->numberScbs; i++)
   {
      virtualAddress = (SCSI_UEXACT8 SCSI_HPTR)hhcb->SCSI_SCB_BUFFER.virtualAddress;
      hhcb->SCSI_FREE_QUEUE[i].scbBuffer.virtualAddress =
         &(virtualAddress[i *SCSI_hSIZE_OF_SCB_BUFFER(hhcb->firmwareMode)]);
#if SCSI_EFI_BIOS_SUPPORT
      busAddress = hhcb->SCSI_SCB_BUFFER.busAddress;
#else
#if SCSI_ASPI_SUPPORT
      OSD_GET_BUS_ADDRESS(hhcb,
            SCSI_MC_LOCKED,hhcb->SCSI_SCB_BUFFER.virtualAddress);
      busAddress = hhcb->busAddress;
#else
      busAddress = OSD_GET_BUS_ADDRESS(hhcb,
            SCSI_MC_LOCKED,hhcb->SCSI_SCB_BUFFER.virtualAddress);
#endif /* SCSI_ASPI_SUPPORT */
#endif /* SCSI_EFI_BIOS_SUPPORT */
      OSD_ADJUST_BUS_ADDRESS(hhcb,&(busAddress),
            (i * SCSI_hSIZE_OF_SCB_BUFFER(hhcb->firmwareMode)));
      hhcb->SCSI_FREE_QUEUE[i].scbBuffer.busAddress = busAddress;
      hhcb->SCSI_FREE_QUEUE[i].scbBuffer.bufferSize = 
            SCSI_hSIZE_OF_SCB_BUFFER(hhcb->firmwareMode);
      SCSI_SET_NULL_SCB(hhcb->SCSI_FREE_QUEUE[i].scbNumber);
  
      /* Link the current buffer to the next one, except the last one which */
      /* does not have to be linked to anything (i.e. it can be dangling). */
      if ((i+1) != hhcb->numberScbs)
      {
         hhcb->SCSI_FREE_QUEUE[i].queueNext = &hhcb->SCSI_FREE_QUEUE[i+1];
      }
   }

   /* If only one SCB is allocated, point the SCB buffer to itself so that */
   /* the allocation/deallocation logic works for both 1 SCB case and more */
   /* than 1 SCB case. */
   if (hhcb->numberScbs == 1)
   {
      hhcb->SCSI_FREE_QUEUE[0].queueNext = &hhcb->SCSI_FREE_QUEUE[0];
   }

#if (SCSI_TARGET_OPERATION && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
   /* Assign errorScbDescriptor */
   if (SCSI_hTARGETASSIGNERRORSCBDESCRIPTOR(hhcb) == SCSI_FAILURE)
   {
      return((SCSI_UINT8)SCSI_FAILURE);
   }
#endif /* (SCSI_TARGET_OPERATION && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */
#endif /* SCSI_SCBBFR_BUILTIN */ 

#if !SCSI_DOWNSHIFT_MODE   
   /* Set memory pointer for queue out pointer array. */
   hhcb->SCSI_QOUT_PTR_ARRAY = (void SCSI_HPTR)
            memoryTable->memory[SCSI_HM_QOUTPTRARRAY].ptr.hPtr;

   totalDoneQElements = 
      (SCSI_UEXACT16)(memoryTable->memory[SCSI_HM_QOUTPTRARRAY].memorySize /
      sizeof(SCSI_QOUTFIFO_NEW));

   if (totalDoneQElements >= SCSI_MINDONEQELEMENTS)
   {
      hhcb->totalDoneQElements = totalDoneQElements;
   }
   else
   {
      status = SCSI_FAILURE;
   }

#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT)
   /* Set memory pointer for SPI Status. */
   hhcb->SCSI_SPI_STATUS = (void SCSI_HPTR)
            memoryTable->memory[SCSI_HM_SPISTATUS].ptr.hPtr;
   /* SPI IU Status buffer for fifio 0 and 1 - null out SCB identifier */
   SCSI_SET_NULL_SCB(hhcb->SCSI_SPI_STATUS_IU_SCB[0]);
   SCSI_SET_NULL_SCB(hhcb->SCSI_SPI_STATUS_IU_SCB[1]);
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT) */

#if SCSI_PACKETIZED_IO_SUPPORT
#if (SCSI_TARGET_OPERATION && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
   /* Set memory pointer for Target Mode Status IU packets. */
   hhcb->SCSI_TM_IU_STATUS_PKTS = (void SCSI_HPTR)
            memoryTable->memory[SCSI_HM_TM_SPISTATUS].ptr.hPtr;

   /* Call routine to initialize this memory area. */
   SCSIhTargetInitSPIStatusBuffers(hhcb);

   /* Set memory pointer for Target Unlocked memory for error handling. */
   hhcb->SCSI_TM_ERROR_UNLOCKED = (void SCSI_HPTR)
            memoryTable->memory[SCSI_HM_TM_ERROR_RECOVERY].ptr.hPtr;   

   /* Create a routine for this memory allocation if more required. */

   /* Assign error HIOB memory */
   hhcb->SCSI_HP_errorHiob = (SCSI_HIOB SCSI_IPTR)hhcb->SCSI_TM_ERROR_UNLOCKED;

   /* Assign error Nexus memory */
   /* Note that COVQ is required beacuse some OS's require pointers on 
    * boundaries.
    */
   hhcb->SCSI_HP_errorNexus =
      (SCSI_NEXUS SCSI_XPTR)((SCSI_UEXACT8 SCSI_HPTR)hhcb->SCSI_TM_ERROR_UNLOCKED +
                             SCSI_COVQ(sizeof(SCSI_HIOB),sizeof(void SCSI_IPTR)));

#endif /* (SCSI_TARGET_OPERATION && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#endif /* !SCSI_DOWNSHIFT_MODE */

   if (hhcb->numberScbs < SCSI_MINSCBS)
   {
      status = SCSI_FAILURE;
   }

   return(status);
}
#endif /* !SCSI_BIOS_SUPPORT */

/*********************************************************************
*
*  SCSIHInitialize
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
*  Remarks:       Usually this routine is called after
*                 SCSIHGetConfiguration, SCSIHGetMemoryTable and
*                 SCSIHSetMemoryTable. It requires both configuration
*                 and memory available in order to initialize
*                 hardware properly.
*                 
*********************************************************************/
SCSI_UINT8 SCSIHInitialize (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 optionMode;
   
   SCSI_hWRITE_HCNTRL(hhcb, SCSI_PAUSE);

#if !SCSI_DCH_U320_MODE
   /* Possible PCI error pre-existing status must be cleared */
   if (SCSIhClearPCIorPCIXError(hhcb))
   {
      return((SCSI_UINT8)(SCSI_PCI_ERROR));
   } 
#endif /* !SCSI_DCH_U320_MODE */

   /* Get the mode now that the sequencer is paused. */
   SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Switch to mode 4 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   optionMode = OSD_INEXACT8(SCSI_AICREG(SCSI_OPTIONMODE));

#if SCSI_TARGET_OPERATION
   if (hhcb->SCSI_HF_targetMode)
   {
#if SCSI_DCH_U320_MODE
      /* Required until DCH sequencer updated. */ 
      if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
#else
      if (SCSI_hHARPOON_REV_A(hhcb))
#endif /* SCSI_DCH_U320_MODE */
      {
         /* The Automatic MSG assertion by the hardware in Dual
          * Edge mode for target operation needs to be disabled. 
          * This is due to a hardware bug where the MSG signal is 
          * incorrectly asserted during a target mode reselection.
          * Razor issue #612. This problem is fixed in Rev B.
          */
         optionMode &= ~SCSI_AUTO_MSGOUT_DE;
      }
   }
#endif /* SCSI_TARGET_OPERATION */

   /* Enable auto-ACK and Bus Free Interrupt Revision features */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_OPTIONMODE), optionMode |
                 (SCSI_AUTOACKEN | SCSI_BUSFREEREV));

#if !SCSI_DCH_U320_MODE
   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      /* The following is a workaround for Rev A of Harpoon2. This is to   */
      /* resolve a synchronization problem between Pclk & clk80 during CIO */
      /* bus access when operated in PCI-X mode.  The details of the       */
      /* hardware problem can be found in the RAZOR database entry #598.   */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_DEVCONFIG1_REG), (SCSI_UEXACT8)
          (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_DEVCONFIG1_REG)) & ~SCSI_PREQDIS));
   }
   
#if !SCSI_ASPI_REVA_SUPPORT
   if (SCSI_hHARPOON_REV_B(hhcb))
   {
      /* The following is a workaround for Rev B of Harpoon2. This is to   */
      /* resolve a done queue corruption problem seen on Compaq Hammer II  */
      /* system.  The problem shows up that the correct done queue content */
      /* has been programmed by sequencer but does not get DMAed to the    */
      /* host memory, instead the previous done queue content gets DMAed   */
      /* into the location for new done queue entry.                       */
      /* When the failure condition happens, the Hapoon2B ASIC still       */
      /* asserts REQ# signal after finishing previous PCI transcation      */
      /* (S/G list DMA).  With the system asserts GNT# signal immediately  */
      /* and Harpoon2B is in the wrong state of its state machine,         */
      /* Harpoon2B sends the data through the PCI bus without waiting the  */
      /* packer logic to pack the correct data.  The reason why Harpoon2B  */
      /* logic gets into this problem is not known at this moment.         */

      /* An observation has been found that when PREQDIS bit (bit 0) of    */
      /* DEVCONFIG1 register in PCI configuration space is set to 1,       */
      /* the problem does not show up.  This is because Harpoon2B ASIC     */
      /* deasserts PREQ# signal on the end of each PCI transcation when    */
      /* PREQDIS bit is set to 1.                                          */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_DEVCONFIG1_REG), (SCSI_UEXACT8)
          (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_DEVCONFIG1_REG)) | SCSI_PREQDIS));
   }
#endif /* !SCSI_ASPI_REVA_SUPPORT */
#endif /* !SCSI_DCH_U320_MODE */

   /* Download sequencer code and reset channel regardless initialization  */
   /* flag is set or not.                                                  */
   /* Setup sequencer for execution */
   if (SCSI_hSETUPSEQUENCER(hhcb))
   {
      return((SCSI_UINT8)(SCSI_SEQUENCER_FAILURE));
   }

   /* initialize hardware */
   SCSIhInitializeHardware(hhcb);

   /* Due to the Bayonet HW that needs to wait a total of 200ms for the    */
   /* scsi bus to stable after the chip reset we must wait for the scsi    */
   /* bus to become stable based on bit ENAB20, ENAB40, and XCVR that is   */
   /* set by the HW after the bus becomes stable                           */
   if (hhcb->SCSI_HF_chipReset)
   {
      if (SCSI_hWAITFORBUSSTABLE(hhcb))
      {
         return ((SCSI_UINT8)(SCSI_TIMEOUT_FAILURE));
      }
   }      

   /* clear hung bus indicator */ 
   SCSI_hSETBUSHUNG(hhcb,0);

   /* check if reset scsi necessary */
   if (hhcb->SCSI_HF_resetSCSI)
   {
      if (SCSIhResetScsi(hhcb))
      {
         SCSI_hSETBUSHUNG(hhcb,1);
         return((SCSI_UINT8)(SCSI_BUS_HUNG_FAILURE));
      }

      /* Delay user specified ammount after scsi reset for slow devices to */
      /* settle down. */
      SCSIhDelayCount25us(hhcb, SCSI_hGETTIMERCOUNT(hhcb,(hhcb->resetDelay*1000)));
   }   
   /* reset channel hardware */
   SCSIhResetChannelHardware(hhcb);

   SCSI_hTARGETMODESWINITIALIZE(hhcb);

   /* initialize hardware/data structures which are dependent on */
   /* mode of operation */
   SCSI_hRESETSOFTWARE(hhcb);

   /* start to run sequencer */
   SCSIhStartToRunSequencer(hhcb);

#if !SCSI_DCH_U320_MODE
#if SCSI_DISABLE_INTERRUPTS   
   SCSI_hWRITE_HCNTRL(hhcb, (SCSI_UEXACT8) 0);
#else
   SCSI_hWRITE_HCNTRL(hhcb, (SCSI_UEXACT8) SCSI_INTEN);
#endif /* SCSI_DISABLE_INTERRUPTS */   

#else /* DCH Support */
#if SCSI_DISABLE_INTERRUPTS   
   SCSI_hWRITE_HCNTRL(hhcb, (SCSI_UEXACT8) 0);
#else
   SCSIHEnableIRQ(hhcb);
#endif /* SCSI_DISABLE_INTERRUPTS */
#endif /* !SCSI_DCH_U320_MODE */

   OSD_SYNCHRONIZE_IOS(hhcb);
#if !SCSI_DCH_U320_MODE 

   if (SCSIhClearPCIorPCIXError(hhcb))
   {
      return((SCSI_UINT8)(SCSI_PCI_ERROR));
   } 
#endif /* !SCSI_DCH_U320_MODE */

   return((SCSI_UINT8)(SCSI_SUCCESS));
}
 
/*********************************************************************
*
*  SCSIhSetupEnvironment
*
*     Setup execution environment
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
void SCSIhSetupEnvironment (SCSI_HHCB SCSI_HPTR hhcb)
{
   /* setup hardware descriptor */
   SCSI_hSETUPHARDWAREDESC(hhcb);

   /* setup firmware descriptor */
   SCSI_hSETUPFIRMWAREDESC(hhcb);
}

/*********************************************************************
*
*  SCSIHSetupHardware
*
*     Setup hardware execution environment
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
void SCSIHSetupHardware (SCSI_HHCB SCSI_HPTR hhcb)
{
   /* setup hardware descriptor */
   SCSI_hSETUPHARDWAREDESC(hhcb);
}

/***************************************************************************
*
*  SCSIhPrepareConfig
*
*     This routine will prepare before collecting configuration information
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine should only be referenced at initialization
*                 time and it's part of initialization block.
*
***************************************************************************/
/* this routine will need to be restructured for a 
   RBI based interface. Whether or not the HDM 
   requires the information provided by this PCI 
   unility is unknown at this time.               */
void SCSIhPrepareConfig (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
#if !SCSI_DCH_U320_MODE
   SCSI_UEXACT32 hostID;
   SCSI_UEXACT32 subSysSubVendID;
#else   
   SCSI_UEXACT16 hostID;
#endif /* !SCSI_DCH_U320_MODE */
   SCSI_UEXACT8 modeSave;

#if !SCSI_BIOS_SUPPORT
   SCSI_UEXACT8 i;
#endif /* !SCSI_BIOS_SUPPORT */

#if !SCSI_DCH_U320_MODE
   subSysSubVendID = OSD_READ_PCICFG_EXACT32(hhcb,SCSI_SSID_SVID_REG);

   /* Check subVendor ID to turn on any OEM support flag */
   if(((SCSI_UEXACT16)subSysSubVendID & SCSI_SVID_MASK) == SCSI_OEM1_SUBVID)
   {
      hhcb->OEMHardware = SCSI_OEMHARDWARE_OEM1;
   }
   else
   {
      /* this is standard hardware */
      hhcb->OEMHardware = SCSI_OEMHARDWARE_NONE;
   }

   hostID = OSD_READ_PCICFG_EXACT32(hhcb,SCSI_ID_REG);
   hhcb->deviceID = (SCSI_UEXACT16) (hostID >> 16);

   /* Comment out the following code for now. If we need
    * to support a disable disconnect U320 board (highly
    * unlikely) then model the code on this algorithm.

#if SCSI_19160_SUPPORT
    * Disable disconnect for HDD on 1960 Disti board. 
       
   if (hhcb->deviceID == SCSI_19160_DISTI)
   {
      hhcb->SCSI_HP_disconnectOffForHDD = 1;
   } 
   else
   {
      hhcb->SCSI_HP_disconnectOffForHDD = 0;
   }

#else
      hhcb->SCSI_HP_disconnectOffForHDD = 0;
#endif  SCSI_19160_SUPPORT
       */
       
   hhcb->SCSI_HP_disconnectOffForHDD = 0;

   /* set up multiFunction flag base on HeaderType multifunction bit */
   hhcb->SCSI_HF_multiFunction =
            ((OSD_READ_PCICFG_EXACT32(hhcb,SCSI_CACHE_LAT_HDR_REG) &
              SCSI_HDRTYPE_MULTIFUNC) == SCSI_HDRTYPE_MULTIFUNC);

#if SCSI_STANDARD_MODE
   if ((hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320) ||
       (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320))
   {
      hhcb->cmdCompleteFactorThreshold = SCSI_CMD_COMPLETE_FACTOR_THRESHOLD;
      hhcb->cmdCompleteThresholdCount = SCSI_CMD_COMPLETE_THRESHOLD_COUNT;
      hhcb->SCSI_HF_cmdCompleteThresholdSupport = 1;
   }
#endif /* SCSI_STANDARD_MODE */

   hhcb->firmwareVersion = (SCSI_UEXACT8) SCSI_hFIRMWARE_VERSION(hhcb);
   hhcb->softwareVersion = (SCSI_UEXACT8) SCSI_SOFTWARE_VERSION;
   hhcb->hardwareRevision = (SCSI_UEXACT8)
                        OSD_READ_PCICFG_EXACT32(hhcb, SCSI_DEV_REV_ID_REG);

#if !SCSI_ASPI_REVA_SUPPORT
   if (SCSI_hHARPOON_REV_B(hhcb))
   {
      /* The following is a workaround for Rev B of Harpoon2. This is to   */
      /* resolve a power management problem under Win2K environment.       */
      /* When the chip is in power management state transition from D3     */
      /* (power down state) to D0 (normal state), Harpoon2 RevB ASIC       */
      /* does not set CHIPRST bit in HCNTRL register.  This causes CHIM    */
      /* code not to set SCSI_HF_chipReset correctly and does not          */
      /* initialize some chip registers as they should be.  The details of */
      /* this hardware issue can be found in the Razor database entry #913.*/

      /* A workaround is implemented to check PCIERRGENDIS bit in          */
      /* PCIERRGEN register to help to decide if chip reset has happened.  */

      if (!hhcb->SCSI_HF_chipReset)
      {
         /* Save the current mode and set to mode 4 */
         modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

         if (!(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_PCIERRGEN_REG)) & SCSI_PCIERRGENDIS))
         {
            hhcb->SCSI_HF_chipReset = 1;
         }

         /* Restore mode pointer */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
      }
   }
#endif /* !SCSI_ASPI_REVA_SUPPORT */   

#else    /* SCSI_DCH_U320_MODE */

   /* set up for these fields is for simulation debug and will certainly
      need to be changed. */
   
   /* Save the mode, and switch to mode 4 */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   /* read the DCH_SCSI device ID reg. */
   hostID = OSD_INEXACT16(SCSI_AICREG(SCSI_DCHVENDORID));
                                      
   hhcb->deviceID = OSD_INEXACT16(SCSI_AICREG(SCSI_DCHDEVICEID));
   /* restore saved mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
   
   hhcb->SCSI_HP_disconnectOffForHDD = 0;

   /* hhcb->SCSI_SUBID_autoTerm = 1; */  /* default to OFF, "0" means ON */
   /* hhcb->SCSI_SUBID_legacyConnector = 0; */ /* @@ appropriate default ?? */
   /* hhcb->SCSI_SUBID_seepromType = SCSI_NO_SEEPROM; */
   
   hhcb->SCSI_HF_multiFunction = 0;
   
   hhcb->firmwareVersion = (SCSI_UEXACT8) SCSI_hFIRMWARE_VERSION(hhcb);
   hhcb->softwareVersion = (SCSI_UEXACT8) SCSI_SOFTWARE_VERSION;

   /* Get the hardware revision from DCH core */
    hhcb->hardwareRevision = 
           (SCSI_UEXACT8)OSD_INEXACT8(SCSI_AICREG(SCSI_DCHREVISION));

#endif /* !SCSI_DCH_U320_MODE */
   /* set the delay time after a SCSI bus reset */
   hhcb->resetDelay = SCSI_RESET_DELAY_DEFAULT;

#if SCSI_TARGET_OPERATION
   if (hhcb->SCSI_HF_targetMode)
   {
      /* Set Data Group CRC generation interval. */
      hhcb->SCSI_HF_targetDGCRCInterval = SCSI_DATA_GROUP_CRC_DEFAULT;
      
#if (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
      /* Set Data Group CRC generation interval. */
      hhcb->SCSI_HF_targetIUCRCInterval = SCSI_IU_CRC_DEFAULT;
#endif /* (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */

      /* Adjust reset delay default if not operating as initiator. */
      if (!hhcb->SCSI_HF_initiatorMode)
      {
         /* normally no delay would be required when operating in 
          * target mode as the target is expected to respond within
          * 250ms after a reset
          */
         hhcb->resetDelay = 0;         
      }
   }
#endif /* SCSI_TARGET_OPERATION */
 
   /* Set the hung channel default time.  */
   hhcb->ioChannelFailureTimeout = SCSI_CHANNEL_TIMEOUT_DEFAULT;
   
   /* Set the HF_ClearConfigurationStatus to a default for clearing  */
   hhcb->SCSI_HF_ClearConfigurationStatus = 1;
   
   /* check if chip reset bit asserted. Pause chip if reset was cleared */
   /* set initialization required flag if it was set */
   if (hhcb->SCSI_HF_chipReset)
   {
      /* Set InitNeeded will either use default or information */
      /* from SEEPROM */
      hhcb->SCSI_HP_initNeeded = 1;
      hhcb->SCSI_HF_resetSCSI = 1;

#if !SCSI_BIOS_SUPPORT
      for (i = 0; i < SCSI_MAXDEV; i++)
      {
         SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DF_resetSCSI = 1;
      }
#endif /* !SCSI_BIOS_SUPPORT */
   }
}

/***************************************************************************
*
*  SCSIhStandardGetConfig
*
*     Get configuration for standard mode
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine should only be referenced at initialization
*                 time and it's part of initialization block.
*
***************************************************************************/
#if SCSI_STANDARD_MODE
void SCSIhStandardGetConfig (SCSI_HHCB SCSI_HPTR hhcb)
{
#if !SCSI_ASPI_SUPPORT
   hhcb->numberScbs = SCSIhGetMaxHardwareScbs(hhcb);
#else
   hhcb->numberScbs = 4;
#endif /* !SCSI_ASPI_SUPPORT */
   hhcb->numberHWScbs = hhcb->numberScbs;
   if (hhcb->numberScbs == SCSI_MAXSCBS)
   {
      hhcb->SCSI_HF_multiTaskLun = 1;
   }

   /* get configuration common to all modes */
   SCSIhGetCommonConfig(hhcb);
}
#endif /* SCSI_STANDARD_MODE */ 

/***************************************************************************
*
*  SCSIhDownshiftGetConfig
*
*     Get configuration for downshift mode
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine should only be referenced at initialization
*                 time and it's part of initialization block.
*
***************************************************************************/
#if SCSI_DOWNSHIFT_MODE
void SCSIhDownshiftGetConfig (SCSI_HHCB SCSI_HPTR hhcb)
{
   hhcb->numberHWScbs = hhcb->numberScbs = 1;
   if (hhcb->numberScbs == SCSI_MAXSCBS)
   {
      hhcb->SCSI_HF_multiTaskLun = 1;
   }

   /* get configuration common to all modes */
   SCSIhGetCommonConfig(hhcb);
}
#endif /* SCSI_DOWNSHIFT_MODE */ 

/*********************************************************************
*
*  SCSIhGetMaxHardwareScbs
*
*     Calculate Initialize hardware
*
*  Return Value:  number of hardware scbs available
*                  
*  Parameters:    hhcb
*
*  Remarks:       At return of this routine sequencer is guaranteed
*                 to be paused
*                  
*********************************************************************/
#if (SCSI_STANDARD_MODE && !SCSI_ASPI_SUPPORT)
SCSI_UEXACT16 SCSIhGetMaxHardwareScbs (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT32 scbData = 0;
   SCSI_UEXACT16 numberScbs = SCSI_MAXSCBS-1;
   SCSI_UEXACT16 blocks;
   SCSI_UEXACT16 scbptr;
   SCSI_UINT16 i;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 modeSave;

   /* check if pause chip necessary */
   if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);      
   }

   /* Save the incoming mode, and switch to mode 3 */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Save current scb pointer */
   scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
             ((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1)) << 8);

#if SCSI_SIMULATION_SUPPORT
   /* When CHIM is run under the simulation environment a PCI error would be */
   /* generated if we read an SCB location in the SCB array before writing   */
   /* to it.  The workaround is to write all the required SCB locations with */
   /* zeros before starting the process of finding out the maximum internal  */
   /* SCBs supported by the hardware.  Since this problem should not occur   */ 
   /* in the real ASIC, this workaround is protected under the 'SIM' compile */
   /* option. */
   for (; numberScbs != 0; numberScbs >>= 1)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)numberScbs);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(numberScbs>>8));
      for (i = 0; i < 4; i++)
      {
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+i),0);
      }
   }
   numberScbs = SCSI_MAXSCBS;
#endif /* SCSI_SIMULATION_SUPPORT */

   /* Load scb pointer with 1FF */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)numberScbs);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(numberScbs>>8));

   for (i = 0; i < 4; i++)             
   {                                   /* Save current contents            */
      scbData |= ((SCSI_UEXACT32) OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+i))) << (i * 8);
   }

   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+0),0xdd);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+1),0x66);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+2),0xaa);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+3),0x55);

   /* Divide scb number by 2 and see if data is mirrored; If data is not mirrored, num=max scb*/
   for (;; numberScbs >>= 1)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)(numberScbs >> 1));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(numberScbs>>9));
      if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+0)) != 0xdd || OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+1)) != 0x66 ||
          OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+2)) != 0xaa || OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+3)) != 0x55)
      {
         break;
      }
   }

   /* Set scb pointer to max scb       */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)numberScbs);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(numberScbs>>8));

   for (i = 0; i < 4; i++)
   {                                   /* Restore back contents            */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+i),
                    (SCSI_UEXACT8)(scbData >> (i * 8)));
   }

   /* Restore current scb pointer */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbptr>>8));

   if (numberScbs != (SCSI_MAXSCBS-1))
   {
      ++numberScbs;
      if ((hhcb->hardwareMode == SCSI_HMODE_AICU320))
      {
         blocks = numberScbs + 1;
         while (blocks)
         {
            numberScbs = blocks - 1;
            blocks &= (blocks - 1);
         }
      } 
   }
   else
   {
      ++numberScbs;
   }

   /* Restore the incoming mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }

   return(numberScbs);
}
#endif /* (SCSI_STANDARD_MODE && !SCSI_ASPI_SUPPORT) */

/***************************************************************************
*
*  SCSIhNonStandardGetConfig
*
*     Get configuration for swapping and test modes
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine should only be referenced at initialization
*                 time and it's part of initialization block.
*
***************************************************************************/
#if SCSI_SWAPPING_MODE
void SCSIhNonStandardGetConfig (SCSI_HHCB SCSI_HPTR hhcb)
{
   hhcb->numberScbs = SCSI_MAXSCBS;

   /* get configuration common to all modes */
   SCSIhGetCommonConfig(hhcb);
}
#endif /* SCSI_SWAPPING_MODE */

/***************************************************************************
*
*  SCSIhGetCommonConfiguration
*
*     Get configuration common to all modes
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine should only be referenced at initialization
*                 time and it's part of initialization block.
*
***************************************************************************/
void SCSIhGetCommonConfig (SCSI_HHCB SCSI_HPTR hhcb)
{
}

/***************************************************************************
*
*  SCSIhGetHardwareConfiguration
*
*     This routine will get harwdare configuration information which are
*     independent of operating mode.
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine should only be referenced at initialization
*                 time and it's part of initialization block.
*
***************************************************************************/
void SCSIhGetHardwareConfiguration (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_UEXACT8 sxfrctl1;
   SCSI_UEXACT8 i;
   SCSI_UEXACT8 sblkctl;
#if (SCSI_SIMULATION_SUPPORT || SCSI_DCH_U320_MODE)
   SCSI_UEXACT8 modeSave;
#endif /* (SCSI_SIMULATION_SUPPORT || SCSI_DCH_U320_MODE) */

#if SCSI_SIMULATION_SUPPORT
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Force wide for Simulation only */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SBLKCTL),
      (OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL)) | SCSI_SELWIDE));

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
#endif /* SCSI_SIMULATION_SUPPORT */

   /* get configuration information which is independent of operating */
   /* mode. */
   /* get default configuration information */
   /* this includes IRQ number selection, threshold, host scsi id */
   /* parity enable/disable, differential board identification, */
   /* wide/narrow selection, fast20/synchronous/asynchronous selection, */
   /* disconnect enable/disable, termination selection. */
#if SCSI_DCH_U320_MODE
   /* The Rocket ASCI may have a problem setting SELWIDE. For now
      just force wide mode by orring SELWIDE into the SBLKCTL reg.   */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Force wide for Simulation only */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SBLKCTL),
      (OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL)) | SCSI_SELWIDE));

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
#endif /* SCSI_DCH_U320_MODE */

   /* Save sblkctl for later use in getting adapter/target profile.           */
   /* CHIM has been updated to not access hardware register for some value    */
   /* during adapter/target profile request.  The change is to fix IO timeout */
   /* exhibit on Harpoon 2 Rev A with the sequencer workaround, ww19, when    */
   /* OSM requests target profile frequently.                                 */
   hhcb->SCSI_HP_sblkctl = sblkctl = OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL));
   if (sblkctl & SCSI_SELWIDE)
   {
      hhcb->maxDevices = 16;
   }
   else
   {
      hhcb->maxDevices = 8;
   }

#if SCSI_SIMULATION_SUPPORT
   /* Allow the user to set the desires number of SCSI IDs
    * scanned by PAC.
    */
   hhcb->maxDevices = SCSI_SIMULATION_MAXDEVICE_COUNT;   
#endif /* SCSI_SIMULATION_SUPPORT */


/* Set the Write Bias Cancelation Control */
#if SCSI_OEM1_SUPPORT
   if (hhcb->OEMHardware == SCSI_OEMHARDWARE_OEM1)
   {
      hhcb->wrtBiasCtl = SCSI_OEM1_WRTBIASCTL_DEFAULT;
   }
   else
#endif /* SCSI_OEM1_SUPPORT */
   {
      hhcb->wrtBiasCtl = SCSI_DEFAULT_WRTBIASCTL;
   } 

   
   for (i = 0; i < hhcb->maxDevices; i++)
   {
      deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];

      /* By default, disconnect is allow */
      deviceTable->SCSI_DF_disconnectEnable = 1;

      /* Make HARDCODE entry to be current negoXferIndex and defaultXferIndex */
      deviceTable->negoXferIndex = SCSI_HARDCODE_ENTRY;
      deviceTable->defaultXferIndex = SCSI_HARDCODE_ENTRY;
      deviceTable->maxXferIndex = SCSI_HARDCODE_ENTRY;

#if SCSI_DOMAIN_VALIDATION
      /* Inititialize pre-DV default transfer index to defaultXferIndex */
      /* The index will be used as a copy of defaultXferIndex in Domain */
      /* Validation implementation.                                     */
      deviceTable->preDVXferIndex = deviceTable->defaultXferIndex;
#endif /* SCSI_DOMAIN_VALIDATION */

      if (sblkctl & SCSI_SELWIDE)
      {
         /* HBA supports wide bus */
         /* By default, we should initiate wide, free running clock, QAS, */
         /* Dual-Transition clocking, and Packetized as well as response  */
         /* to them. */
         SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_MISC_OPT] = SCSI_WIDE;
#if !SCSI_DOWNSHIFT_MODE 
         SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] =
                                             (SCSI_QUICKARB | SCSI_DUALEDGE);

#if SCSI_PACKETIZED_IO_SUPPORT
         /* Update RTI and RD Streaming in ID based configuration.*/
         SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] |= 
             (SCSI_PCOMP_EN | SCSI_WR_FLOW | SCSI_HOLD_MCS | SCSI_PACKETIZED);

#if SCSI_TARGET_OPERATION
         if (hhcb->SCSI_HF_targetMode && (!hhcb->SCSI_HF_initiatorMode))
         {
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
            if (SCSI_hHARPOON_REV_1B(hhcb))
            {
               /* Supports packetized but not SCSI_WR_FLOW or SCSI_RD_STRM 
                * when target mode only.
                */
               SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= 
                  (~(SCSI_WR_FLOW | SCSI_RD_STRM | SCSI_HOLD_MCS));
            }
            else
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
            {
               /* Does not support any packetized features when target mode
                * only (i.e. Only supports DT and QAS).
                */
               SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= 
                  (SCSI_DT_REQ | SCSI_QAS_REQ);
            }
         }
#endif /* SCSI_TARGET_OPERATION */
             
#if SCSI_PROTOCOL_OPTION_MASK_ENABLE 
         deviceTable->SCSI_DF_ptlOptMaskEnable = 1;
#else
         deviceTable->SCSI_DF_ptlOptMaskEnable = 0;
#endif /* SCSI_PROTOCOL_OPTION_MASK_ENABLE */


#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#else         
         SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] = 0;
#endif /* !SCSI_DOWNSHIFT_MODE */

#if SCSI_LOOPBACK_OPERATION
         /* Packetized operation is not supported in target mode. Therefore,
          * ensure packetized related fields are disabled. In theory, QAS
          * could also be enabled, but no point as no other devices
          * allowed on the bus when using the Loopback feature.
          */
         SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &=
            SCSI_DUALEDGE;
#endif /* SCSI_LOOPBACK_OPERATION */
      }
      else
      {
         /* HBA supports narrow bus */
         /* By default, we should not initiate wide, free running clock, */
         /* QAS, Dual-Transition clocking, and Packetized as well as     */
         /* response to them. */
         SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] = 0;
         SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_MISC_OPT] = 0;
      }

      /* Initialize all targets to be single-transition clocking, */
      /* narrow, and asynchronous. */
      SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
      SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;
      SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] = 0;
      SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_MISC_OPT] = 0;

      /* Initialize the current xfer entry to be single-transition clocking, */
      /* narrow, and asynchronous. */
      SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
      SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;
      SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] = 0;
      SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_MISC_OPT] = 0;

      /* Initialize the last negotiated xfer entry to be single-transition */
      /* clocking, narrow, and asynchronous. */
      SCSI_LAST_NEGO_XFER(deviceTable)[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
      SCSI_LAST_NEGO_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;
      SCSI_LAST_NEGO_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] = 0;
      SCSI_LAST_NEGO_XFER(deviceTable)[SCSI_XFER_MISC_OPT] = 0;

#if SCSI_NEGOTIATION_PER_IOB
      /* Clear all 'forceNego' flags */
      deviceTable->SCSI_DNF_forceSync = 0;
      deviceTable->SCSI_DNF_forceAsync = 0;
      deviceTable->SCSI_DNF_forceWide = 0;
      deviceTable->SCSI_DNF_forceNarrow = 0;
#endif /* SCSI_NEGOTIATION_PER_IOB */

      /* Set Host Managed by default */
      deviceTable->SCSI_DF_hostManaged = 1;

      /* Use same level of Precomp across all hardware */
      deviceTable->SCSI_DEP_precomp = SCSI_PRECOMP_DEFAULT;

      /* Use the default values as opposed to programed values */
      /* Per adapter or target profile */
      deviceTable->SCSI_DEP_userSlewRate = 0;

#if !SCSI_ASPI_REVA_SUPPORT
      if (SCSI_hHARPOON_REV_B(hhcb) || SCSI_hDCH_SCSI_CORE(hhcb))
      {
#if SCSI_OEM1_SUPPORT
         /* Assuming we will have different values for OEM1 */   
         if (hhcb->OEMHardware == SCSI_OEMHARDWARE_OEM1)
         {
            deviceTable->SCSI_DEP_slewrate = SCSI_DEFAULT_OEM1_HARPOON2B_SLEWRATE;
            deviceTable->SCSI_DEP_amplitude = SCSI_DEFAULT_OEM1_HARPOON2B_AMPLITUDE;
         }
         else
#endif /* SCSI_OEM1_SUPPORT */
         {
            deviceTable->SCSI_DEP_slewrate = SCSI_DEFAULT_HARPOON2B_SLEWRATE;
            deviceTable->SCSI_DEP_amplitude = SCSI_DEFAULT_HARPOON2B_AMPLITUDE;
         }

#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
         if (SCSI_hHARPOON_REV_1B(hhcb) &&
             hhcb->SCSI_HF_targetMode &&
             (!hhcb->SCSI_HF_initiatorMode))
         {
            /* Set the upper 4 bits of NEGODAT to our SCSI ID for 
             * multiple target ID support.
             */
            /* Offset to location in auto-rate-offset table by specified target ID */
            SCSI_hSETELECINFORMATION(hhcb,
                                     (SCSI_UEXACT8)(i | (hhcb->hostScsiID << 4)),
                                     SCSI_CURRENT_XFER(deviceTable));
         }
         else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
         {
            SCSI_hSETELECINFORMATION(hhcb,i,SCSI_CURRENT_XFER(deviceTable));
         }
      }
#endif /* !SCSI_ASPI_REVA_SUPPORT */
   }

   /* collect configuration which are id based */
   SCSIhGetIDBasedConfiguration(hhcb);

   /* Initialize threshold setting */ 
   hhcb->SCSI_HF_separateRWThresholdEnable = 0;

   if (hhcb->SCSI_HP_initNeeded)
   {
      /* use default configuration */
      hhcb->threshold = SCSI_hSETDATAFIFOTHRSHDEFAULT(hhcb);
      hhcb->hostScsiID = 7;
#if SCSI_PARITY_PER_IOB      
      /* Always turn off parity checking first for per iob parity checking */
      /* implementation. Parity will be turned on from sequencer base on   */
      /* iob's parityEnable flag                                           */ 
      hhcb->SCSI_HF_scsiParity = 0;
#else
      hhcb->SCSI_HF_scsiParity = 1;
#endif /* SCSI_PARITY_PER_IOB */
      /* Set selection timeout delay to default. */  
      hhcb->SCSI_HF_selTimeout = SCSI_256MS_SELTO_DELAY;

      /* Generally CacheLineStreaming will be enabled by default   */
      /* This feature will be turned off if a specific hw does not */
      /* support it correctly.                                     */
#if !SCSI_DCH_U320_MODE
      hhcb->SCSI_HF_cacheThEn = SCSI_CACHETHEN_DEFAULT;
#endif /* !SCSI_DCH_U320_MODE */
   }
   else
   {
      /* use existing hardware configuration */
      hhcb->threshold = SCSI_hGETDATAFIFOTHRSH(hhcb);
      hhcb->hostScsiID = (OSD_INEXACT8(SCSI_AICREG(SCSI_IOWNID)) & SCSI_IOID);
      sxfrctl1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
      hhcb->SCSI_HF_scsiParity = (sxfrctl1 & SCSI_ENSPCHK) ? 1 : 0;
      hhcb->SCSI_HF_selTimeout = SCSIhGetSeltoDelay(hhcb);
#if !SCSI_DCH_U320_MODE
      hhcb->SCSI_HF_cacheThEn = SCSI_hGETCACHETHEN(hhcb);
#endif /* !SCSI_DCH_U320_MODE */
   }

   hhcb->SCSI_HF_expSupport = SCSI_EXPACTIVE_DEFAULT;

}

/***************************************************************************
*
*  SCSIhGetIDBasedConfiguration
*
*     Collect configuration information which are id based
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine should only be referenced at initialization
*                 time and it's part of initialization block.
*
***************************************************************************/
void SCSIhGetIDBasedConfiguration (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_UEXACT8 i;

   SCSI_hSETDEFAULTAUTOTERM(hhcb);

   /* By default we should set the SCSI termination power level */
   /* in DEVCONFIG0 register to active low. */
   hhcb->SCSI_HF_terminationLevel = 1;

#if SCSI_AUTO_TERMINATION
   hhcb->SCSI_HP_autoTerminationMode = SCSI_AUTOTERM_MODE2_CABLE_SENSING;
#endif /* SCSI_AUTO_TERMINATION */

   /* No expander active support by default */
   hhcb->SCSI_HF_expSupport = 0;

   /* Harpoon supports separate read/write thresholds */
   hhcb->SCSI_HF_separateRWThreshold = 1;
      
   for (i = 0; i < hhcb->maxDevices; i++)
   {
      deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];

      /* Initialize the default transfer period based on ASIC Device ID */
      if ((hhcb->deviceID & SCSI_PN_MASK) == SCSI_U320_ASIC)
      {
         /* FAST-160 (U320) */
         /* By default, we should initiate synchronous xfer. */
         /* as well as response synchronous xfer. */
#if !SCSI_DOWNSHIFT_MODE

#if SCSI_OEM1_SUPPORT
         /* default to allow QAS for all devices */
         hhcb->SCSI_HF_adapterQASDisable = 0;
#endif /* SCSI_OEM1_SUPPORT */

#if SCSI_PACKETIZED_IO_SUPPORT
         SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PERIOD] = 0x08;

         SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] |= 
               (SCSI_RD_STRM | SCSI_QAS_REQ);

#if !SCSI_ASPI_REVA_SUPPORT
         if (SCSI_hHARPOON_REV_B(hhcb) || SCSI_hDCH_SCSI_CORE(hhcb))
         { 
            /* Enable RTI for Rev B only */
            SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] |= SCSI_RTI;
         }
#endif /* !SCSI_ASPI_REVA_SUPPORT */         

#if SCSI_TARGET_OPERATION
         if (hhcb->SCSI_HF_targetMode && (!hhcb->SCSI_HF_initiatorMode))
         {
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
            if (SCSI_hHARPOON_REV_1B(hhcb))
            {
               /* Supports packetized but not SCSI_WR_FLOW or SCSI_RD_STRM 
                * when target mode only.
                */
               SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= 
                  (~(SCSI_WR_FLOW | SCSI_RD_STRM | SCSI_HOLD_MCS));
            }
            else
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
            {
               /* Does not support any packetized features when target mode
                * only.
                */
               /* No IU support when operating in target mode only. */
               SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PERIOD] = 0x09;

               /* Only DT and QAS supported */
               SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= 
                  (SCSI_DT_REQ | SCSI_QAS_REQ);
            }           
         }
#endif /* SCSI_TARGET_OPERATION */

#else /* !SCSI_PACKETIZED_IO_SUPPORT */
         /* If non-packetized fastest rate is Fast-80, period 09h. */
         SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PERIOD] = 0x09;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

#else /* SCSI_DOWNSHIFT_MODE */
         SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PERIOD] = 0x0A;
#endif /* !SCSI_DOWNSHIFT_MODE */
         SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_OFFSET] = SCSI_MAX_OFFSET;
      }
      else
      {
         /* By default, we should not initiate synchronous xfer. */
         /* as well as respond to synchronous xfer. */
         SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
         SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;
         break;
      }
   }
}

/***************************************************************************
*
*  SCSIhStandardU320SetupSequencer
*
*     Setup sequencer code to run standard Ultra 320 mode
*
*  Return Value:  return status
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine should only be referenced at initialization
*                 time and it's part of initialization block.
*
***************************************************************************/
#if SCSI_STANDARD_U320_MODE
SCSI_UEXACT8 SCSIhStandardU320SetupSequencer (SCSI_HHCB SCSI_HPTR hhcb)
{
   return(SCSI_hLOADSEQUENCER(hhcb,Seqs320,sizeof(Seqs320)));
}
#endif /* SCSI_STANDARD_U320_MODE */

/***************************************************************************
*
*  SCSIhStandardEnhU320SetupSequencer
*
*     Setup sequencer code to run standard enhanced Ultra 320 mode
*
*  Return Value:  return status
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine should only be referenced at initialization
*                 time and it's part of initialization block.
*
***************************************************************************/
#if SCSI_STANDARD_ENHANCED_U320_MODE
SCSI_UEXACT8 SCSIhStandardEnhU320SetupSequencer (SCSI_HHCB SCSI_HPTR hhcb)
{
   return(SCSI_hLOADSEQUENCER(hhcb,Seqse320,sizeof(Seqse320)));
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/***************************************************************************
*
*  SCSIhDchU320SetupSequencer
*
*     Setup sequencer code to run DCH Ultra 320 mode
*
*  Return Value:  return status
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine should only be referenced at initialization
*                 time and it's part of initialization block.
*
***************************************************************************/
#if SCSI_DCH_U320_MODE
SCSI_UEXACT8 SCSIhDchU320SetupSequencer (SCSI_HHCB SCSI_HPTR hhcb)
{
   return(SCSI_hLOADSEQUENCER(hhcb,Dchs320,sizeof(Dchs320)));
}
#endif /* SCSI_DCH_U320_MODE */

/***************************************************************************
*
*  SCSIhDownshiftU320SetupSequencer
*
*     Setup sequencer code to run downshift Ultra 320 mode
*
*  Return Value:  return status
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine should only be referenced at initialization
*                 time and it's part of initialization block.
*
***************************************************************************/
#if SCSI_DOWNSHIFT_MODE
SCSI_UEXACT8 SCSIhDownshiftU320SetupSequencer (SCSI_HHCB SCSI_HPTR hhcb)
{
#if SCSI_BIOS_SUPPORT
   return(SCSIhPIOLoadSequencer(hhcb,
                                hhcb->SCSI_HP_seqAddress,
                                hhcb->SCSI_HP_seqSize));
#else
#if SCSI_DOWNSHIFT_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
   {
      return(SCSI_hLOADSEQUENCER(hhcb,Seqd320,sizeof(Seqd320)));
   }
#endif /* SCSI_DOWNSHIFT_U320_MODE */
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)
   {
      return(SCSI_hLOADSEQUENCER(hhcb,Seqde320,sizeof(Seqde320)));
   }
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */
#endif /* SCSI_BIOS_SUPPORT */

   return(SCSI_ASIC_FAILURE); 
}
#endif /* SCSI_DOWNSHIFT_MODE */

/*********************************************************************
*
*  SCSIhDMALoadSequencer
*
*     Load sequencer code using DMA
*
*  Return Value:  0 - sequencer code loaded and verified
*                 others - sequencer code verification failed
*
*  Parameters:    hhcb
*                 address of sequencer code to be loaded
*                 size of sequencer code
*
*  Remarks:      Incoming Mode : Don't care
*                                Assumes ASIC is paused. 
*                  
*********************************************************************/
#if (!(SCSI_BIOS_MODE_SUPPORT || SCSI_SEQ_LOAD_USING_PIO))
SCSI_UEXACT8 SCSIhDMALoadSequencer (SCSI_HHCB SCSI_HPTR hhcb,
                                    SCSI_UEXACT8 SCSI_LPTR seqCode,
                                    SCSI_UEXACT32 seqSize)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT32 cnt;
   SCSI_UEXACT8 seqctl;
   SCSI_UEXACT8 modeSave;
   SCSI_UEXACT8 result = 0;
   SCSI_UINT32 i = 0;
   SCSI_UEXACT32 bufCnt, bufSize, downloadSize, totalDMAs;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 simode0;
   SCSI_UEXACT8 simode1;
   SCSI_UEXACT8 simode2;
   SCSI_UEXACT8 lqimode0; 
   SCSI_UEXACT8 lqimode1;
   SCSI_UEXACT8 lqomode0;
   SCSI_UEXACT8 lqomode1;
   SCSI_UEXACT32 SCSI_LPTR buf;
   SCSI_UINT32 j; 

   /* Save the incoming mode, and switch to mode 3 */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Save SEQCTL */
   seqctl = OSD_INEXACT8(SCSI_AICREG(SCSI_SEQCTL0));

   /* Save the HCNTRL register as we will be changing it during download */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* Disable all SCSI interrupts - saving interrupt masks. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

   simode0 = OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE0));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE0), 0);
   simode1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE1));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE1), 0);
   simode2 = OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE2));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE2), 0);
   lqimode0 = OSD_INEXACT8(SCSI_AICREG(SCSI_LQIMODE0));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQIMODE0), 0);
   lqimode1 = OSD_INEXACT8(SCSI_AICREG(SCSI_LQIMODE1));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQIMODE1), 0);
   lqomode0 = OSD_INEXACT8(SCSI_AICREG(SCSI_LQOMODE0));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQOMODE0), 0);
   lqomode1 = OSD_INEXACT8(SCSI_AICREG(SCSI_LQOMODE1));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQOMODE1), 0);
   /* End of disable all SCSI interrupts */

   /* Clear any possible interrupts */
#if !SCSI_DCH_U320_MODE
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), 
                 (SCSI_CLRBRKADRINT | SCSI_CLRSWTMINT |
                  SCSI_CLRSCSIINT | SCSI_CLRARPINT |
                  SCSI_CLRCMDINT | SCSI_CLRSPLTINT)); 

#else /* SCSI_DCH_U320_MODE */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), 
                 (SCSI_CLRBRKADRINT | SCSI_CLRSWERRINT |
                  SCSI_CLRSCSIINT | SCSI_CLRARPINT)); 

#endif /* !SCSI_DCH_U320_MODE */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
#if !SCSI_DCH_U320_MODE
   /* Download the loader code using PIO at the end of the sequencer RAM */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_OVLYADDR0),
      (SCSI_UEXACT8)((((SCSI_UEXACT32)SCSI_SEQCODE_SIZE -
                      SCSI_hLOAD_SIZE(hhcb))/4)&(SCSI_UEXACT8)0xFF));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_OVLYADDR1),
      (SCSI_UEXACT8)((((SCSI_UEXACT32)SCSI_SEQCODE_SIZE -
                       SCSI_hLOAD_SIZE(hhcb))/4)>>8));

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

   for (cnt = 0; cnt < SCSI_hLOAD_SIZE(hhcb); cnt++)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQRAM), SCSI_hLOAD_CODE(hhcb)[cnt]);
   }
#else /* SCSI_DCH_U320_MODE */
   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_OVLYADDR0),
      (SCSI_UEXACT8)((((SCSI_UEXACT32)SCSI_SEQCODE_SIZE -
                      (SCSI_UEXACT32)sizeof(Dchload))/4)&(SCSI_UEXACT8)0xFF));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_OVLYADDR1),
      (SCSI_UEXACT8)((((SCSI_UEXACT32)SCSI_SEQCODE_SIZE -
                       (SCSI_UEXACT32)sizeof(Dchload))/4)>>8));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0),
                 (SCSI_FASTMODE + SCSI_PERRORDIS + SCSI_LOADRAM));
   for (cnt = 0; cnt < (SCSI_UEXACT32)sizeof(Dchload); cnt++)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQRAM), Dchload[cnt]);
   }
   
#endif /* !SCSI_DCH_U320_MODE  */
   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0),
                    (SCSI_FASTMODE + SCSI_PERRORDIS));
   }

   /* Get a buffer of type SCSI_MC_LOCKED, as the incoming seqCode array is  */
   /* in SCSI_MC_UNLOCKED memory. DMA can be done only from a LOCKED memory. */
   buf = (SCSI_UEXACT32 SCSI_LPTR)(hhcb->SCSI_SCB_BUFFER.virtualAddress);
   /* Assumes contiguous scb buffer area. */
   bufSize = SCSI_hSIZE_OF_SCB_BUFFER(hhcb->firmwareMode) * hhcb->numberScbs;

#if (!SCSI_DCH_U320_MODE && (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE))
   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      /* Harpoon2 Rev A chip has a problem (razor issue #385) wherein the size */
      /* of the DMA cannot be more than 0xFC bytes.  This workaround reduces   */
      /* the size to 0xFC bytes, if needed. */
      if (bufSize > (SCSI_UEXACT32)0xFC)
      {
         bufSize = (SCSI_UEXACT32)0xFC;
      }
   }
#endif /* (!SCSI_DCH_U320_MODE && (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE))   */

   /* Calculate the total number of DMA's that are needed, depending on the */
   /* size of the buffer */
   totalDMAs = (seqSize+bufSize-1)/bufSize;

   /* Do each DMA by copying the chunk of bytes from seqCode array into the */
   /* buffer and unpausing the sequencer (the loader code). */
   for (bufCnt = 0; bufCnt < totalDMAs; bufCnt++)
   {
      /* Calculate the correct count of bytes to be DMAed, as it might vary */
      /* for the last DMA */
      if (bufCnt == (totalDMAs-1))
      {
         downloadSize = seqSize-(bufSize*bufCnt);
      }
      else
      {
         downloadSize = bufSize;
      }

      /* Move the bytes from the seqCode array into the buffer. */
      for (cnt = 0; cnt < (downloadSize/4); cnt++)
      {
         buf[cnt] = ((SCSI_UEXACT32 SCSI_LPTR)seqCode)[cnt+(bufCnt*bufSize/4)];
      }

#if (!SCSI_DCH_U320_MODE && (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE))
      if (SCSI_hHARPOON_REV_A(hhcb))
      {
         /* Harpoon2 Rev A chip has a problem where the OVERLAY address does  */
         /* not point to the address after the last byte of the previous DMA. */
         /* This causes corruption of data stored in the sequencer RAM.  To   */
         /* work around this problem, the correct address is manually written */
         /* every time before starting the DMA. */
         /* REV B address S/B 00 from rollover from PIO of loader code.*/
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_OVLYADDR0),
               (SCSI_UEXACT8)(bufCnt*bufSize/4));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_OVLYADDR1),
               (SCSI_UEXACT8)((bufCnt*bufSize/4)>>8));
      }

#endif /* (!SCSI_DCH_U320_MODE && (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE))  */
      /* Load the address of the buffer into the scratch memory. */
      SCSI_hSETADDRESSSCRATCH(hhcb,SCSI_hLOAD_ADDR(hhcb),
                              hhcb->SCSI_SCB_BUFFER.virtualAddress);

      /* Load the count of bytes to be downloaded into the scratch memory. */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_hLOAD_COUNT(hhcb)+0),
                         (SCSI_UEXACT8)downloadSize);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_hLOAD_COUNT(hhcb)+1),
                         (SCSI_UEXACT8)((downloadSize)>>8));

#if !SCSI_DCH_U320_MODE 
      /* Set the program counter to the start of the loader code */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
         (SCSI_UEXACT8)((((SCSI_UEXACT32)SCSI_SEQCODE_SIZE-
                         SCSI_hLOAD_SIZE(hhcb))/4)&(SCSI_UEXACT8)0xFF));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
         (SCSI_UEXACT8)((((SCSI_UEXACT32)SCSI_SEQCODE_SIZE-
                          SCSI_hLOAD_SIZE(hhcb))/4)>>8));
         
#else /* SCSI_DCH_U320_MODE */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
         (SCSI_UEXACT8)((((SCSI_UEXACT32)SCSI_SEQCODE_SIZE-
                         (SCSI_UEXACT32)sizeof(Dchload))/4)&(SCSI_UEXACT8)0xFF));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
         (SCSI_UEXACT8)((((SCSI_UEXACT32)SCSI_SEQCODE_SIZE-
                          (SCSI_UEXACT32)sizeof(Dchload))/4)>>8));
#endif /* !SCSI_DCH_U320_MODE */
      /* Unpause the loader and disable interrupt */

      i = 0;
#if SCSI_DCH_U320_MODE
      SCSI_hWRITE_HCNTRL(hhcb, (hcntrl & ~(SCSI_PAUSE)));
      
#else      
      SCSI_hWRITE_HCNTRL(hhcb, (hcntrl & ~(SCSI_PAUSE | SCSI_INTEN)));
#endif /* SCSI_DCH_U320_MODE */

      j = (SCSI_REGISTER_TIMEOUT_COUNTER);
      /* Wait till you get the completion status or time out */
      while (!(OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT)) & SCSI_ARPINT) &&
#if SCSI_SIMULATION_SUPPORT
             (i++ < (SCSI_REGISTER_TIMEOUT_COUNTER*10)))
#else
#if SCSI_DCH_U320_MODE
             (i++ < (SCSI_REGISTER_TIMEOUT_COUNTER*3)))
#else      

             (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
#endif /* SCSI_DCH_U320_MODE */
#endif /* SCSI_SIMULATION_SUPPORT */
      {
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }

      /* If DMA has completed successfully, clear the completion interrupt, */
      /* else return error. */
#if SCSI_SIMULATION_SUPPORT
      if ((i < (SCSI_REGISTER_TIMEOUT_COUNTER*10)) &&
#else
      if ((i < SCSI_REGISTER_TIMEOUT_COUNTER) &&
#endif /* SCSI_SIMULATION_SUPPORT */
          (OSD_INEXACT8(SCSI_AICREG(SCSI_ARPINTCODE)) == SCSI_LOAD_DONE))
      {
         /* clear SEQ interrupt */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRARPINT);
      }
      else
      {
         /* Set failure result. */
         result = SCSI_ASIC_FAILURE;
      }
   }

   /* Restore interrupt masks. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE0), simode0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE1), simode1);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE2), simode2);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQIMODE0), lqimode0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQIMODE1), lqimode1);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQOMODE0), lqomode0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQOMODE1), lqomode1);
   /* End of restore interrupt masks. */

   /* Restore the HCNTRL register */
   SCSI_hWRITE_HCNTRL(hhcb, hcntrl);

   /* Reinitialize the scb buffer used to download sequencer. */ 
   OSDmemset(buf,0,bufSize);

   /* Restore SEQCTL */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0), seqctl);

   /* Restore the incoming mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   return(result);
}
#endif /* (!(SCSI_BIOS_MODE_SUPPORT || SCSI_SEQ_LOAD_USING_PIO)) */

/*********************************************************************
*
*  SCSIhPIOLoadSequencer
*
*     Load sequencer code using PIO
*
*  Return Value:  0 - sequencer code loaded and verified
*                 others - sequencer code verification failed
*
*  Parameters:    hhcb
*                 address of sequencer code to be loaded
*                 size of sequencer code 
*
*  Remarks:      Incoming Mode : Don't care
*                                Assumes ASIC is paused. 
*                  
*********************************************************************/
#if (SCSI_BIOS_MODE_SUPPORT || SCSI_SEQ_LOAD_USING_PIO)
SCSI_UEXACT8 SCSIhPIOLoadSequencer (SCSI_HHCB SCSI_HPTR hhcb,
                                    SCSI_UEXACT8 SCSI_LPTR seqCode,
                                    SCSI_UEXACT32 seqSize)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT32 cnt;
   SCSI_UEXACT8 seqctl;
   SCSI_UEXACT8 modeSave;
   SCSI_UEXACT8 result = 0;

   /* Save the incoming mode, and switch to mode 3 */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Save SEQCTL */
   seqctl = OSD_INEXACT8(SCSI_AICREG(SCSI_SEQCTL0));

   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Load the sequencer code from the array into the sequencer RAM */
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

   for (cnt = 0; cnt < seqSize; cnt++)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQRAM), seqCode[cnt]);
   }

   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0),
                    (SCSI_FASTMODE + SCSI_PERRORDIS));
   }

   /* Read back the sequencer code from the RAM and check whether  */
   /* the bytes written were correct; If there is mismatch, return */
   /* error. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_OVLYADDR0), 00);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_OVLYADDR1), 00);

   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0),
                    (SCSI_FASTMODE + SCSI_PERRORDIS + SCSI_LOADRAM));
   }

   for (cnt = 0; cnt < seqSize; cnt++)
   {
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_SEQRAM)) != seqCode[cnt])
      {
         result = SCSI_ASIC_FAILURE;
         break;
      }
   }

   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0),
                    (SCSI_FASTMODE + SCSI_PERRORDIS));
   }

   /* Restore SEQCTL */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0), seqctl);

   /* Restore the incoming mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   return(result);
}
#endif /* (SCSI_BIOS_MODE_SUPPORT || SCSI_SEQ_LOAD_USING_PIO) */

/***************************************************************************
*
*  SCSIhInitializeHardware
*
*     This routine will initialize hardware configuration which is
*     required at initialization time
*
*  Return Value:  return status
*                  
*  Parameters:    host task set handle
*
*  Remarks:       This routine should only be referenced at initialization
*                 time and it's part of initialization block.
*
*                 Assume we are in Mode 3.
*
***************************************************************************/
void SCSIhInitializeHardware (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
#if !SCSI_UPDATE_TERMINATION_ENABLE
   SCSI_UEXACT8 stpwenSetting = 0;
#endif /* !SCSI_UPDATE_TERMINATION_ENABLE */
#if !SCSI_DCH_U320_MODE
   SCSI_UEXACT32 devConfig0;
#endif /* !SCSI_DCH_U320_MODE */
   SCSI_UEXACT8 modeSave; 
   SCSI_UEXACT8 i; 
   
/* if HWM interfaced && Enhanced Error Checking... enable PERR/SPLT interrupts */
#if (defined(SCSI_INTERNAL) && (!SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING))
   /* get status and command from hardware */
   SCSI_UEXACT32 statCmd = OSD_READ_PCICFG_EXACT32(hhcb,SCSI_STATUS_CMD_REG);

   if (!(statCmd & SCSI_PERRESPEN))
   {
      OSD_WRITE_PCICFG_EXACT32(hhcb,SCSI_STATUS_CMD_REG,(statCmd | SCSI_PERRESPEN));
   }

#endif /* (defined(SCSI_INTERNAL)) && (!SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING)) */

   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

#if SCSI_SIMULATION_SUPPORT
   /* Cut down the DIFFSENSE filter and SELTIMEOUT timers to allow faster */
   /* simulation */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSITEST), SCSI_CNTRTEST);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Force wide for Simulation only */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SBLKCTL),
      (OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL)) | SCSI_SELWIDE));
#endif /* SCSI_SIMULATION_SUPPORT */

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_IOWNID), hhcb->hostScsiID);

   if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
   {
      /* Must set TOWNID to Initiator ID also. There is an ASIC bug
       * that results in a physical error (parity error) interrupt
       * with DTERR set in PERRDIAG on a reselection from a device
       * that matches the value in TOWNID, in this case device with
       * SCSI ID 0 if we don't set register. This is due
       * to the following:
       *   Selecting device zero when your own TOWNID register is zero.
       *   There is logic in the chip to take the value on the bus during
       *   (re)selection, mask off the IOWNID bit, mask off the TOWNID bit,
       *   and see if anything is left.  In this case, there is nothing
       *   left. The logic then determines that this is an old-style
       *   "select in without ID" (shouldn't happen as we are initiator,
       *   not target - ASIC bug). No Razor entry yet.
       */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_TOWNID), hhcb->hostScsiID); 
   }

   /* Turn off led */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SBLKCTL),
       (OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL)) &
            ~(SCSI_DIAGLEDEN + SCSI_DIAGLEDON)));

   SCSI_hUPDATEDATAFIFOTHRSH(hhcb);

   /* Turn on digital filter and digital filtering period expanded. */
   /* The bits are defined for both H2A4 and H2B.  But they do not  */
   /* affect anything in H2A4.  They affect H2B and single-ended    */
   /* bus only.                                                     */
   /*   H2A4 and H2B has DSP sampler and Bypass circuitry.          */
   /*   The original design is to use the DSP sampler for LVD/SE    */
   /*   signal.  Bypass circuitry is for debug purpose.  But we     */
   /*   encountered problem in H2A4.  So, hardware engineers decided*/
   /*   to route the SE to the Bypass circuitry until we have a fix */
   /*   for DSP sampler.  However, the fix might take a risk on LVD */
   /*   site. So, hardware engineers make the Bypass circuitry as   */
   /*   a permanent fix for SE in both H2A4 and H2B.  However, there*/
   /*   is one difference.  And it is the filter has been added in  */
   /*   H2B for SE but there isn't one in H2A.  This might tell us  */
   /*   why the Vulcan team saw the hang in SE running at 20MHz     */
   /*   under H2A4.                                                 */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL0), (SCSI_DFON | SCSI_DFPEXP));

#if !SCSI_DCH_U320_MODE   /* it is undetermined at this time what if any support is
                           required when using the DCH_SCSI core */ 
   SCSI_hUPDATECACHETHEN(hhcb);

   if (hhcb->SCSI_HF_ForceSTPWLEVELforEmbeddedChips)
   {
      /* Device id for Harpoon 2 Rev B based boards, ASC-39320D HBA and   */
      /* ASC-39320D iROC, do not follow previous PCI device id convention */
      /* such that we need to do special check here.                      */
#if SCSI_IROC
      if (((hhcb->deviceID & SCSI_EMBEDDED_ID) == SCSI_EMBEDDED_ID) &&
          (hhcb->deviceID != SCSI_39320D_REVB_IROC))
#else /* !SCSI_IROC */
      if (((hhcb->deviceID & SCSI_EMBEDDED_ID) == SCSI_EMBEDDED_ID) &&
          (hhcb->deviceID != SCSI_39320D_REVB))
#endif /* SCSI_IROC */
      {
         devConfig0 = OSD_READ_PCICFG_EXACT32(hhcb,SCSI_DEVCONFIG0_REG);
         OSD_WRITE_PCICFG_EXACT32(hhcb,SCSI_DEVCONFIG0_REG,(devConfig0 | SCSI_STPWLEVEL));
      }
   }
   else
   {
      /* Set the SCSI termination power level to the correct level */
      /* (low or high) based on the  information stored in NVRAM.  */
      devConfig0 = OSD_READ_PCICFG_EXACT32(hhcb,SCSI_DEVCONFIG0_REG);
      if (hhcb->SCSI_HF_terminationLevel)
      { 
         devConfig0 |= SCSI_STPWLEVEL;
      }
      else
      {
         devConfig0 &= ~SCSI_STPWLEVEL;
      }
      OSD_WRITE_PCICFG_EXACT32(hhcb,SCSI_DEVCONFIG0_REG,devConfig0);
   }
#endif /* !SCSI_DCH_U320_MODE */ 
   
   /* Enable LQI and LQO Manager fatal interrupts */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   
   for (i = 0; i < 0x14; i++)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DSPSELECT), i);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_WRTBIASCTL), hhcb->wrtBiasCtl);
   }

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQIMODE1),(SCSI_LQIPHASE1MSK |
                                             SCSI_LQIPHASE2MSK |
                                             SCSI_LQIABORTMSK  |
                                             SCSI_LQICRCI1MSK  |
                                             SCSI_LQICRCI2MSK  |
                                             SCSI_LQIBADLQIMSK |
                                             SCSI_LQIOVERI1MSK |
                                             SCSI_LQIOVERI2MSK));

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQOMODE1),(SCSI_ENLQOBUSFREE1 | SCSI_ENLQOBADQAS | SCSI_ENLQOPHACHG1));

#if SCSI_INTERNAL_PARITY_CHECK
   /* Set up Data Parity Check Enable */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DSCOMMAND0),
      (OSD_INEXACT8(SCSI_AICREG(SCSI_DSCOMMAND0)) | SCSI_DPARCKEN)); 
#endif /* SCSI_INTERNAL_PARITY_CHECK */

   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
   /* Initialize the SEQCTL register to enable all ASIC failure
    * detection logic.
    */
/*********************************************************************/
/* FAILDIS is no longer controlled by error handling compile options */          
/*********************************************************************/

/* #if SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING  */
   /* When this compile option disallows handling of HWERRINT, SPLTINT or a
      PCIINT interrupt, FAILDIS is enabled. This will prevent any of these error
      conditions from pausing the Sequencer and forcing HIM interaction.
     */  
/*   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0), SCSI_FASTMODE + SCSI_FAILDIS);
#else
*/ 
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0), SCSI_FASTMODE);
/* #endif */ /* SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */

#if SCSI_AUTO_TERMINATION
   /* do auto termination if user enables */
   SCSI_hPROCESSAUTOTERM(hhcb);
#endif /* SCSI_AUTO_TERMINATION */

#if SCSI_UPDATE_TERMINATION_ENABLE

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
   /* Check if the chip's reset. */
   if (hhcb->SCSI_HF_chipReset)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), SCSI_STPWEN);
   }

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), (SCSI_UEXACT8)
           ((hhcb->SCSI_HF_scsiParity ? SCSI_ENSPCHK : 0) +
            SCSI_ENSTIMER + SCSI_ACTNEGEN +    /* low byte termination get set here */
            (hhcb->SCSI_HF_primaryTermLow ? SCSI_STPWEN : 0)));

   /* Parity and CRC checking are two separated control bits: ENSPCHK and */
   /* CRCVALCHKEN respectively.  CHIM will disable/enable parity and CRC  */
   /* checking based on SCSI_HF_scsiParity flag. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   if (hhcb->SCSI_HF_scsiParity)
   {
      /* Enable CRC checking */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CRCCONTROL),
          (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_CRCCONTROL)) | SCSI_CRCVALCHKEN));
   }
   else
   {
      /* Disable CRC checking */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CRCCONTROL),
          (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_CRCCONTROL)) & ~SCSI_CRCVALCHKEN));
   }
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Selection timeout delay set separately. */   
   SCSIhSetSeltoDelay(hhcb);

   /* update termination that controlled by external hardware logic */
   SCSI_hUPDATEEXTTERM(hhcb);

#else /* !SCSI_UPDATE_TERMINATION_ENABLE */

#if SCSI_CUSTOM_TERMINATION
   if(hhcb->SCSI_HF_primaryTermLow || hhcb->SCSI_HF_primaryTermHigh)
   {
      stpwenSetting = SCSI_STPWEN;
   }
   else
   {
      stpwenSetting = 0;
   }

   /* Set the SCSI Termination Power Level to Active Low */
   /* This should be performed in the OSM code!! */
   devConfig0 = OSD_READ_PCICFG_EXACT32(hhcb,SCSI_DEVCONFIG0_REG);
   devConfig0 |= SCSI_STPWLEVEL;
   OSD_WRITE_PCICFG_EXACT32(hhcb,SCSI_DEVCONFIG0_REG, devConfig0);

   /* Set up SCSI_SXFRCTL1;  set the termination */
   stpwenSetting += (hhcb->SCSI_HF_scsiParity ? SCSI_ENSPCHK : 0) +
                     SCSI_ENSTIMER + SCSI_ACTNEGEN;

   /* It was discovered that the STPWCTL logic used by the BIOS and driver */
   /* in controlling the on-board SCSI terminators was not properly        */
   /* initialized following a chip reset.  The key here is 'Chip Reset'.   */
   /* Due to the ambiguity of the data book, it was not clear that the     */
   /* STPWCTL output signal can become active only after first writing a   */
   /* '1' to the STPWEN bit (bit 0 of SXFRCTL1 reg.) following a chip      */
   /* reset.  Writing the '1' to STPWEN enables the output logic of STPWCTL*/
   /* signal.  Otherwise the output of STPWCTL remains tri-stated and does */
   /* not reflect the state of STPWEN as programmed by BIOS and driver.    */
   /* This might result in over termination problem on the SCSI Bus because*/
   /* the BIOS and driver cannot disable on-board SCSI termination.        */
   /* Therefore, CHIM must write a '1' first to STPWEN bit of SXFRCTL1     */
   /* reg. to enable the tri-state output buffer before setting the state  */
   /* of the termination control following a chip reset.                   */
   /* Check if the chip's reset. */
   if (hhcb->SCSI_HF_chipReset)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), SCSI_STPWEN);
   }

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1),stpwenSetting);

   /* Parity and CRC checking are two separated control bits: ENSPCHK and */
   /* CRCVALCHKEN respectively.  CHIM will disable/enable parity and CRC  */
   /* checking based on SCSI_HF_scsiParity flag. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   if (hhcb->SCSI_HF_scsiParity)
   {
      /* Enable CRC checking */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CRCCONTROL),
          (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_CRCCONTROL)) | SCSI_CRCVALCHKEN));
   }
   else
   {
      /* Disable CRC checking */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CRCCONTROL),
          (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_CRCCONTROL)) & ~SCSI_CRCVALCHKEN));
   }
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Selection timeout delay set separately. */   
   SCSIhSetSeltoDelay(hhcb);

#else /* !SCSI_CUSTOM_TERMINATION */

   /* keep the current STPWEN setting */
   stpwenSetting = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1)) & SCSI_STPWEN;
#if SCSI_ASPI_SUPPORT
   if((hhcb->deviceID == SCSI_39320A) && (hhcb->SCSI_HF_primaryTermLow || hhcb->SCSI_HF_primaryTermHigh))
   {
      stpwenSetting = SCSI_STPWEN;
   }
   else
   {
      stpwenSetting = 0;
   }
#endif /* SCSI_ASPI_SUPPORT */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), (SCSI_UEXACT8)
           ((hhcb->SCSI_HF_scsiParity ? SCSI_ENSPCHK : 0) +
            SCSI_ENSTIMER + SCSI_ACTNEGEN +
            stpwenSetting));

   /* Parity and CRC checking are two separated control bits: ENSPCHK and */
   /* CRCVALCHKEN respectively.  CHIM will disable/enable parity and CRC  */
   /* checking based on SCSI_HF_scsiParity flag. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   if (hhcb->SCSI_HF_scsiParity)
   {
      /* Enable CRC checking */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CRCCONTROL),
          (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_CRCCONTROL)) | SCSI_CRCVALCHKEN));
   }
   else
   {
      /* Disable CRC checking */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CRCCONTROL),
          (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_CRCCONTROL)) & ~SCSI_CRCVALCHKEN));
   }
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Selection timeout delay set separately. */   
   SCSIhSetSeltoDelay(hhcb);

#endif /* SCSI_CUSTOM_TERMINATION */
#endif /* SCSI_UPDATE_TERMINATION_ENABLE */


   /* Harpoon2 Rev A chip has a problem where the initial state of the bits  */
   /* in the per-device bytes 0,1,2 and 3 are unknown after a CHIPRST, which */
   /* may result in spurious NTRAMPERR interrupt.  The workaround is to      */
   /* initialize all the 4 per-device bytes to zero, to avoid the interrupt. */
#if (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)
   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXCOL), SCSI_PERDEVICE0);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXDAT), (SCSI_UEXACT8)0);
      /* Write 0 to per-device byte 1. Note: the ANNEXCOL auto increments. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXDAT), (SCSI_UEXACT8)0);
      /* Write 0 to per-device byte 2. Note: the ANNEXCOL auto increments. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXDAT), (SCSI_UEXACT8)0);
      /* Write 0 to per-device byte 3. Note: the ANNEXCOL auto increments. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXDAT), (SCSI_UEXACT8)0);
   }
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE */
   SCSI_hTARGETMODEHWINITIALIZE(hhcb);

   /* DIFFSENS signal needs 200ms to settle down after chip reset, this is done  */
   /* in ASIC's filter logic.  After setting up termination, the same filter     */
   /* will also kick in if there is DIFFSENS signal change.                      */
   /* 100 ms filter time delay is added before CHIM initialization code continues*/
   /* This is to address an issue that while nothing attached to the conector,   */
   /* some terminator chip makes our ASIC think it's in SE mode.  After the      */
   /* termination setting up, it's actually in LVD mode and ASIC reports IOERROR */

   /* A problem is seen on Harpoon2 HBA with Hot Plug environment.  The problem  */
   /* is seen on either Rev A or Rev B based HBA with TI terminator chip on it.  */
   /* The problem is that after Hot Add HBA into a system, the first command to  */
   /* the device will not get selection signal out on the SCSI bus properly which*/
   /* causes the driver to time-out the command.                                 */
   /* The root cause of the problem is not known at this moment.                 */
   /* A temporary workaround is implemented in CHIM code to detect if BIOS is not*/
   /* present and it's HBA id, then CHIM does 750 ms delay to avoid the problem. */
   /* The reason to check BIOS not present is in Hot Plug environment, BIOS code */
   /* is not invoked.                                                            */
   /* Notice device id for Harpoon 2 Rev B based boards, ASC-39320D HBA and      */
   /* ASC-39320D iROC, do not follow previous PCI device id convention such      */
   /* that we need to do special check here.                                     */

#if !SCSI_DCH_U320_MODE
#if SCSI_IROC
   if ((!hhcb->SCSI_HP_biosPresent) && 
       (((hhcb->deviceID & SCSI_EMBEDDED_ID) != SCSI_EMBEDDED_ID) ||
        (hhcb->deviceID == SCSI_39320D_REVB_IROC)))
#else /* !SCSI_IROC */
   if ((!hhcb->SCSI_HP_biosPresent) && 
       (((hhcb->deviceID & SCSI_EMBEDDED_ID) != SCSI_EMBEDDED_ID) ||
        (hhcb->deviceID == SCSI_39320D_REVB)))
#endif /* SCSI_IROC */
   {
      SCSIhDelayCount25us(hhcb,SCSI_hGETTIMERCOUNT(hhcb,30000));  /* delay 750ms */
   }
   else
   {
      SCSIhDelayCount25us(hhcb,SCSI_hGETTIMERCOUNT(hhcb,4000));   /* delay 100ms */
   }
#else /* SCSI_DCH_U320_MODE */
   /* unsure as to how this might effect a DCH core implemtation. */
   /* For now add the 100ms delay. $$$ DCH $$$                    */
   SCSIhDelayCount25us(hhcb,SCSI_hGETTIMERCOUNT(hhcb,4000));      /* delay 100ms */
#endif /* !SCSI_DCH_U320_MODE */

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
}

/***************************************************************************
*
*  SCSIhStartToRunSequencer
*
*     Start to run sequencer
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       All registers accessed in this routine support all
*                 modes, therefore no mode changes are required.
*
***************************************************************************/
void SCSIhStartToRunSequencer (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

   /* Entry point is always low page. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                 (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                 (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));
   
   /* Clear chipReset copy in HHCB as well */
   hhcb->SCSI_HF_chipReset = 0;

   /* Clear chipInitialized flag to indicate that we need reset */
   /* auto rate table if scsi event occurred during run-time.   */
   hhcb->SCSI_HF_chipInitialized = 0;
}

/***************************************************************************
*
*  SCSIhProcessAutoTerm
*
*     Process auto termination if user enables.
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:
*
***************************************************************************/
#if SCSI_AUTO_TERMINATION
void SCSIhProcessAutoTerm (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_UEXACT8 terminationSetting;

  /* if user enables primary autotermination */
   if (hhcb->SCSI_HF_primaryAutoTerm)
   {
      /* For the Harpoon, the HW autotermination logic detects */
      /* SCSI cable connections, decodes and provides the      */
      /* termination control bits for the software to read.    */
             
      terminationSetting = SCSIhCableSense(hhcb);
      hhcb->SCSI_HF_primaryTermLow =
         (terminationSetting & SCSI_TERM_PRIMARY_LOW) ? 1 : 0;
      hhcb->SCSI_HF_primaryTermHigh =
         (terminationSetting & SCSI_TERM_PRIMARY_HIGH) ? 1 : 0;

      /* if user enables secondary autotermination */
      if (hhcb->SCSI_HF_secondaryAutoTerm)
      {
         hhcb->SCSI_HF_secondaryTermLow =
            (terminationSetting & SCSI_TERM_SECONDARY_LOW) ? 1 : 0;
         hhcb->SCSI_HF_secondaryTermHigh =
            (terminationSetting & SCSI_TERM_SECONDARY_HIGH) ? 1 : 0;
      }
   }

   /* if user enables secondary autotermination */
   else if (hhcb->SCSI_HF_secondaryAutoTerm)
   {
      terminationSetting = SCSIhCableSense(hhcb);
      hhcb->SCSI_HF_secondaryTermLow =
         (terminationSetting & SCSI_TERM_SECONDARY_LOW) ? 1 : 0;
      hhcb->SCSI_HF_secondaryTermHigh =
         (terminationSetting & SCSI_TERM_SECONDARY_HIGH) ? 1 : 0;
   }
}
#endif /* SCSI_AUTO_TERMINATION */

/***************************************************************************
*
*  SCSIhUpdateExtTerm
*
*     Update termination that controlled via external hardware logic.
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       The primary high and low byte termination and/or
*                 secondary high and low byte termination will be updated.
*
***************************************************************************/
#if SCSI_UPDATE_TERMINATION_ENABLE
void SCSIhUpdateExtTerm (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_UEXACT8 scsiBrddat = 0;

   /* primary high byte termination disable/enable only if it's wide bus */
   if ((hhcb->maxDevices == 16) && (hhcb->SCSI_HF_primaryTermHigh))
   {
      scsiBrddat |= SCSI_TERM_PRIMARY_HIGH;
   }

   if (hhcb->SCSI_HF_primaryTermLow)
   {
      scsiBrddat |= SCSI_TERM_PRIMARY_LOW;
   }
   
   if (hhcb->SCSI_HF_secondaryTermLow)
   {
      scsiBrddat |= SCSI_TERM_SECONDARY_LOW;
   }

   if (hhcb->SCSI_HF_secondaryTermHigh)
   {
      scsiBrddat |= SCSI_TERM_SECONDARY_HIGH;
   }

   /* process primary high byte, secondary low and high byte termination */
  
   /* write access to Harpoon flexport status reg 0*/
   SCSIhWriteFlexRegister(hhcb,SCSI_FLXCSTREG0,scsiBrddat);
}
#endif /* SCSI_UPDATE_TERMINATION_ENABLE */

/*********************************************************************
*
*  SCSIhReadFlexRegister
*
*     Read from parameter selected flexport register.
*     Flexport released.
*
*  Return Value:  Value read from flex board data
*                  
*  Parameters:    hhcb
*                 Flexport register address decode. 
*
*  Remarks:       This routine executes flex port protocol to acquire,     
*                 read 1 byte and then releases the flexport.
*                 Mode 3 required.
*                 
*********************************************************************/
#if (!SCSI_DCH_U320_MODE && SCSI_AUTO_TERMINATION)
SCSI_UEXACT8 SCSIhReadFlexRegister (SCSI_HHCB SCSI_HPTR hhcb,
                                    SCSI_UEXACT8 regvalue)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 brddata;

   /* Request read access to Harpoon flexport status reg 1 */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDCTL),
         (SCSI_BRDEN | regvalue | SCSI_BRDRW)); 

   /* Check for the gain access to Flexport */
   SCSI_hCHECKFLEXPORTACCESS(hhcb);

   brddata = OSD_INEXACT8(SCSI_AICREG(SCSI_BRDDAT));
   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDCTL), 0x00);  /* deassert all */
   return(brddata);
}
#endif /* (!SCSI_DCH_U320_MODE && SCSI_AUTO_TERMINATION) */

/*********************************************************************
*
*  SCSIhWriteFlexRegister
*
*     Write data from parameter selected flexport register.
*     Flexport released.
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 Flexport register address decode. 
*                 board data value
*
*  Remarks:       This routine executes flex port protocol to acquire,     
*                 read 1 byte and then releases the flexport.  
*                 Mode 3 required.
*                 
*********************************************************************/
#if (!SCSI_DCH_U320_MODE && (SCSI_CURRENT_SENSING || SCSI_UPDATE_TERMINATION_ENABLE))
void SCSIhWriteFlexRegister (SCSI_HHCB SCSI_HPTR hhcb,
                             SCSI_UEXACT8 regvalue,
                             SCSI_UEXACT8 brddata)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 savedMode;

   /* Save the current mode */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   /* Set the mode to 3 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Acquire Flexport */
   /* Request write access to Harpoon flexport */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDCTL),(SCSI_BRDEN | regvalue)); 
   
   /* Check for the gain access to Flexport */
   SCSI_hCHECKFLEXPORTACCESS(hhcb);
        
   /* Set data */     
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDDAT),brddata);

   /* Toggle board strobe */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDCTL), 
         (SCSI_BRDEN | regvalue | SCSI_BRDSTB)); 
         
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDCTL), 
         (SCSI_BRDEN | regvalue )); 

   /* Release Flexport */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDCTL), 0);

   /* Restore the saved mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
}
#endif /* (!SCSI_DCH_U320_MODE && (SCSI_CURRENT_SENSING || SCSI_UPDATE_TERMINATION_ENABLE)) */

/*********************************************************************
*
*  SCSIhGetSeltoDelay
*
*     Get the Selection Timeout Delay from the hardware register
*
*  Return Value:  The delay value to be stored or interpreted. 
*                 One of:
*                    SCSI_256MS_SELTO_DELAY   -  256ms
*                    SCSI_128MS_SELTO_DELAY   -  128ms 
*                    SCSI_64MS_SELTO_DELAY    -   64ms
*                    SCSI_32MS_SELTO_DELAY    -   32ms 
*
*  Parameters:    hhcb
*
*  Remarks:       Assumes the sequencer is paused. 
*                  
*********************************************************************/
SCSI_UEXACT8 SCSIhGetSeltoDelay (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 modeSave;
   SCSI_UEXACT8 sxfrctl1;

   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Isolate Selection Timeout bits. */
   sxfrctl1 = ((OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1)) &
                (SCSI_STIMESEL1 + SCSI_STIMESEL0)) >> 3);
#if (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)
   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      /* Harpoon2 Rev A has a problem wherein the selection time-out time 
       * becomes twice the value that is programmed in the SXFRCTL1
       * register.  The workaround is to reduce this programmed value by
       * one to get the desired time-out on the bus. The details of this
       * hardware problem can be found in the Razor database entry #486.
       * This means that value SCSI_SELTO_32 is mapped onto
       * 64ms for now as only 3 values are available.
       * i.e. ALTSIM   STIMSEL1    STIMESEL0        Delay
       *         0        0            0            512ms
       *         0        0            1            256ms
       *         0        1            0            128ms
       *         0        1            1             64ms
       */
      /* Adjust register value by -1. */
      if (sxfrctl1 != SCSI_256MS_SELTO_DELAY)
      {
         /* Adjust for all values other than 0 (256ms)
          * which should never be set anyway.
          */
         sxfrctl1--;
      }
   }
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE) */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   return (sxfrctl1);
}

/*********************************************************************
*
*  SCSIhWaitForBusStable
*
*     Wait for the SCSI bus stable by checking ENAB20 and ENAB40 bit.
*
*  Return Value:  0  Bus is stable
*                 -1 Bus is not stable
*
*  Parameters:    hhcb - pointer to hhcb structure
*
*  Remarks:
*
*********************************************************************/
SCSI_UEXACT8 SCSIhWaitForBusStable (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UINT32 count;                        /* time out counter */
   SCSI_UEXACT8 sblkctl, returnValue;
   SCSI_UEXACT8 modeSave;

#if (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)
   SCSI_UEXACT8 simode0;
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE) */
   struct
   {
      SCSI_UEXACT8 preValue:1;
      SCSI_UEXACT8 curValue:1;
      SCSI_UEXACT8 Reserved:6;
   } clkout;

   /* Due to the Bayonet HW that needs to wait a total of 200ms for the scsi */
   /* bus to stable after the chip reset.  Before this time, it does not */
   /* allow any scsi signals to send to scsi bus.  Therefore, we must wait */
   /* for the scsi bus stable as well base on bit ENAB20, ENAB40 AND XCVR */
   /* that set by the HW when it confirmed that the scsi bus is stabled. */
   /* This is a true table that is used to determine for the SCSI Bus status */
   /* ENAB40  ENAB20  XCVR  Means */
   /* ------  ------  ----  ----- */
   /*   0       0      X    I/O Disable */
   /*   1       0      0    LVD */
   /*   1       0      1    Illegal */
   /*   0       1      0    SE */
   /*   0       1      1    HVD */
   /*   1       1      X    Illegal */  
   /*                                 */
   /* These bits are driven by the SCSI DIFFSENSE analog signal pin thru */
   /* voltage comparator to determine for SCSI voltage mode.  When the */ 
   /* on-board termination is turned off, the SCSI DIFFSENSE is floating, */
   /* and it may drive the invalid value for ENAB40, ENAB20, and XCVR bits */
   /* based on the floating voltage value of the termination chip in used. */
   /* Therefore, the function may return the good status when the bus is */
   /* unstabled. Based on the experiment, the termination chip DALLAS DS2119M */
   /* return SE mode when turn off termination, and the termination chip */
   /* DALLAS DS2118MB return I/O Disable when turn off termination */
   /*                                               */
   /* CLKOUT provides a 102.40us period from a 40MHz clock-in source. */
   /* The hardware will wait for a total of 200ms before it turn on the */
   /* ENAB20, ENAB40 and XCVR.  So, HIM will use at least 400ms for time-out */
   /* to cover some delay might happen. */

   /* count will be decremented by 1 at every time the clock out bit changed */
   /* its value.  Thus for every one CLKOUT period, the cnt will be */
   /* decremented by 2. */
   count = 8000;

   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Get the current value of clkout to start with */
   clkout.preValue =
            (OSD_INEXACT8(SCSI_AICREG(SCSI_TARGIDIN)) & SCSI_CLKOUT) ? 1 : 0;

   while (1)
   {
      sblkctl = OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL));
      if ((sblkctl & (SCSI_ENAB20 | SCSI_ENAB40 | SCSI_XCVR)) == SCSI_ENAB40)
      {
         returnValue = 0;
         break;
      }
      if ((sblkctl & (SCSI_ENAB20 | SCSI_ENAB40)) == SCSI_ENAB20)
      {
         returnValue = 0;
         break;
      }
      clkout.curValue =
            (OSD_INEXACT8(SCSI_AICREG(SCSI_TARGIDIN)) & SCSI_CLKOUT) ? 1 : 0;

      /* Check if current clock out value changes since last one */
      if (clkout.curValue ^ clkout.preValue)
      {
         /* Update new clkout value */
         clkout.preValue = clkout.curValue;
         if (!(--count))
         {
            /* time-out has reached */
            returnValue = (SCSI_UEXACT8)-1;
            break;
         }
      }
   }

   if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
   {
      /* Harpoon2 Rev A chip has two problems in the I/O cell:               */
      /* 1. When the SCSI bus is in Single-Ended mode the I/O cell does not  */
      /*    sample the SCSI signals.  The workaround is to enable the bypass */
      /*    after a power-on chip reset and on an IOERR interrupt. The       */
      /*    details of this hardware problem can be found in the Razor       */
      /*    database entry #493.                                             */
      /* 2. When the SCSI bus is in LVD mode the I/O cell sampling module    */
      /*    does not get initialized properly.  The workaround is to enable  */
      /*    the bypass after a power-on chip reset and on an IOERR interrupt */
      /*    and disable the bypass after the first selection done or         */
      /*    selection timeout. The details of this hardware problem can be   */
      /*    found in the Razor database entry #491.                          */
      /* Harpoon2 Rev B chip has only the problem# 1.  Therefore workaround  */
      /* for problem# 2 is applied for only the Rev A chip.                  */
      /* Here we enable the bypass which satisfies both of these problems.   */
      /* Also, we enable the Selection done interrupt to occur, as we need   */
      /* clear the bypass for the second problem after the first selection   */
      /* is done. */
      /* If target mode is enabled then a selection-in is most likely the    */
      /* first bus activity. Therefore, also enable selection-in interrupt if*/
      /* target mode is enabled. The handling will be the same as is done    */
      /* for SELDO.                                                          */  

      /* Is the bus in SE mode? */ 
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL)) & SCSI_ENAB20)
      {
         /*  Yes. Apply the workaround for problem# 1 for both Rev A and B. */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DSPDATACTL),
            OSD_INEXACT8(SCSI_AICREG(SCSI_DSPDATACTL)) | SCSI_BYPASSENAB);
      }

#if (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)

      /* No. the bus is in LVD mode.  Apply the workaround for problem #2,  */
      /* which is for Rev A chip only. */
      else if (SCSI_hHARPOON_REV_A(hhcb))
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DSPDATACTL),
            OSD_INEXACT8(SCSI_AICREG(SCSI_DSPDATACTL)) | SCSI_BYPASSENAB);

         /* Read SIMODE0 & enable ENSELDO interrupt. */
         simode0 = OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE0)) | SCSI_ENSELDO;
      
#if SCSI_TARGET_OPERATION
         if (hhcb->SCSI_HF_targetMode)
         {
            /* Enable selection-in interrupt also (SELDI). */
            simode0 |= SCSI_ENSELDI;
         }
#endif /* SCSI_TARGET_OPERATION */

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE0),simode0);
      }
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE) */
   }

   /* Restore back the original mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   return (returnValue);
}

/*********************************************************************
*
*  SCSIhGetCacheThEn
*
*     Get the CacheThEn status
*
*  Return Value:  0 - Cache Threshold Disable
*                 1 - Cache Threshold Enable
*
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
#if !SCSI_DCH_U320_MODE
SCSI_UEXACT8 SCSIhGetCacheThEn (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 modeSave;
   SCSI_UEXACT8 retVal = 0;

   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

   if ((OSD_INEXACT8(SCSI_AICREG(SCSI_DSCOMMAND0)) & SCSI_CACHETHEN))
   {
      retVal = 1;
   }
   else
   {
      retVal = 0;
   }

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   return (retVal);
}
#endif /* !SCSI_DCH_U320_MODE */

/***************************************************************************
*
*  SCSIhGetDataFifoThrsh
*
*     Get Data Fifo threshold. 
*
*  Return Value:  threshold 
*                  
*  Parameters:    hhcb
*
*  Remarks:
*
***************************************************************************/
SCSI_UEXACT8 SCSIhGetDataFifoThrsh (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 threshold;
   SCSI_UEXACT8 savedMode;
   
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   
   threshold = OSD_INEXACT8(SCSI_AICREG(SCSI_DFF_THRSH));

   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
   
   return(threshold);
}

/*********************************************************************
*
*  SCSIhUpdateCacheThEn
*
*     Update CACHETHEN bit appropriately
*
*  Return Value:  None
*
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
#if !SCSI_DCH_U320_MODE
void SCSIhUpdateCacheThEn (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 savedMode;

   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

   if (hhcb->SCSI_HF_cacheThEn)
   {
      /* Enable cache line streaming */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DSCOMMAND0),
          (OSD_INEXACT8(SCSI_AICREG(SCSI_DSCOMMAND0)) | SCSI_CACHETHEN)); 
   }
   else
   {
      /* Disable cache line streaming */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DSCOMMAND0),
          (OSD_INEXACT8(SCSI_AICREG(SCSI_DSCOMMAND0)) & ~SCSI_CACHETHEN)); 
   }

   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
}
#endif /* !SCSI_DCH_U320_MODE */

/***************************************************************************
*
*  SCSIhSetDataFifoThrshDefault
*
*     Set Data Fifo threshold default.
*
*  Return Value:  threshold
*                  
*  Parameters:    hhcb
*
*  Remarks:
*
***************************************************************************/
SCSI_UEXACT8 SCSIhSetDataFifoThrshDefault (SCSI_HHCB SCSI_HPTR hhcb)
{
   return((SCSI_UEXACT8)(SCSI_WR_DFT_DEFAULT + SCSI_RD_DFT_DEFAULT));
}

/***************************************************************************
*
*  SCSIhUpdateDataFifoThrsh
*
*     Update Data Fifo threshold. 
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:
*
***************************************************************************/
void SCSIhUpdateDataFifoThrsh (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 savedMode;
   
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFF_THRSH), hhcb->threshold);
   
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
}

/*********************************************************************
*
*  SCSIhGetNegoData
*
*     Get the SCSI transfer parameters: PERIOD, OFFSET,
*     PPR's protocol option, and WIDE from NEGODAT registers
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 targetID
*                 transfer rate to be filled
*
*  Remarks:
*                  
*********************************************************************/
void SCSIhGetNegoData (SCSI_HHCB SCSI_HPTR hhcb,
                       SCSI_UEXACT8 targetID,
                       SCSI_UEXACT8 SCSI_LPTR xferRate)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 modeSave;
   SCSI_UEXACT8 hcntrl;

   /* Save the HCNTRL value. */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* Pause the chip, if necessary. */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Offset to location in auto-rate-offset table by specified target ID */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGOADDR), targetID);

   /* Get scsi transfer period */
   xferRate[SCSI_XFER_PERIOD] = OSD_INEXACT8(SCSI_AICREG(SCSI_NEGODATA0));

   /* Get QAS, dual edge, and packetized bits */
   xferRate[SCSI_XFER_PTCL_OPT] = OSD_INEXACT8(SCSI_AICREG(SCSI_NEGODATA2));

#if (!SCSI_DOWNSHIFT_MODE && (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE))
   /* Harpoon 2 Rev A ASIC uses a vlaue of 0x07 to represent Fast-160. */
   /* However SPI-4 spec defines 0x08 for this rate, so modify stored  */
   /* rate to match SPI-4 specifaction.  The details of this hardware  */
   /* problem can be found in the Razor database entry #85.            */
   if (SCSI_hHARPOON_REV_A(hhcb))
   {
   
      if (xferRate[SCSI_XFER_PERIOD] == 0x07)
      {
         xferRate[SCSI_XFER_PERIOD] = 0x08;

         /* Mask off SPI4 bit. */ 
         xferRate[SCSI_XFER_PTCL_OPT] &= (~SCSI_SPI4);
      }
   }
#endif /* (!SCSI_DOWNSHIFT_MODE && (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)) */

   /* Get scsi transfer offset */
   xferRate[SCSI_XFER_OFFSET] = OSD_INEXACT8(SCSI_AICREG(SCSI_NEGODATA1));

#if (!SCSI_DOWNSHIFT_MODE && (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE))
   /* The latest SPI-4 specification requires that each data valid   */
   /* state REQ assertion during paced DT data phase corresponds to  */
   /* 4 bytes of data.  But Harpoon2 Rev A chip assumes only 2 bytes */
   /* per REQ assertion.  The workaround is to reduce the maximum    */
   /* offset negotiated by half and programming NEGODAT1 register    */
   /* with twice the negotiated offset.  Razor database entry #560.  */
   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      if (xferRate[SCSI_XFER_PERIOD] <= 0x08)
      {
         xferRate[SCSI_XFER_OFFSET] /= 2;
      }
   }
#endif /* (!SCSI_DOWNSHIFT_MODE && (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)) */

   /* Get wide transfer */
   xferRate[SCSI_XFER_MISC_OPT] = OSD_INEXACT8(SCSI_AICREG(SCSI_NEGODATA3));

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   /* Restore HCNTRL, if necessary. */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
}

/*********************************************************************
*
*  SCSIhCableSense
*
*     Read the internal/external cable configuration status
*
*  Return Value:  cableStat
*                  
*  Parameters:    hhcb
*
*  Remarks:       The only auto termination scheme assumed is cable sensing.
*                 This routine is called from initialize hardware where a
*                 mode save then restore is performed.
*                  
*********************************************************************/
#if SCSI_AUTO_TERMINATION
SCSI_UEXACT8 SCSIhCableSense (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 cableStat = 0;
   SCSI_UEXACT8 hcntrl;   
   SCSI_UEXACT8 modeSave;
   
   /* save the HCNTRL value */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));
   
   /* pause the chip if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   /* Mode 3 required */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
   switch (hhcb->SCSI_HP_autoTerminationMode)
   {
      case SCSI_AUTOTERM_MODE0_CABLE_SENSING:
         break;

      case SCSI_AUTOTERM_MODE1_CABLE_SENSING:     
         break;

      case SCSI_AUTOTERM_MODE2_CABLE_SENSING:      /* Harpoon base   */
      
         /* Request read access to Harpoon flexport status reg 0*/
   
         /* check external arbitration enable bit status */
         /* Harpoon Based Ctl/Stat reg 0      */
         
         /* Bit 3 - SCSI_TERM_SECONDARY_HIGH  */
         /* Bit 2 - SCSI_TERM_SECONDARY_LOW   */
         /* Bit 1 - SCSI_TERM_PRIMARY_HIGH    */
         /* Bit 0 - SCSI_TERM_PRIMARY_LOW     */
         
         cableStat = SCSIhReadFlexRegister(hhcb, SCSI_FLXCSTREG0);
         break;

      default:
         /* Should never reach here. */
         break;
   }
   
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);   /* Restore mode */
   
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
   return(cableStat);
}
#endif /* SCSI_AUTO_TERMINATION */

/*********************************************************************
*
*  SCSIhCheckFlexPortAccess
*
*     Check for the gain access to the FLEXPort
*
*  Return Value:  None
*
*  Parameters:    hhcb
*
*  Remarks:       0: Flexport access disabled
*                 1: Flexport access enabled
*
*********************************************************************/
#if (!SCSI_DCH_U320_MODE && (SCSI_CURRENT_SENSING || SCSI_UPDATE_TERMINATION_ENABLE || SCSI_AUTO_TERMINATION))
void SCSIhCheckFlexPortAccess (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UINT32 i = 0;          /* time out counter */

   while ((!(OSD_INEXACT8(SCSI_AICREG(SCSI_BRDCTL)) & SCSI_FLXARBACK)) &&
          (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
   {
      OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
   }
}   
#endif /* (!SCSI_DCH_U320_MODE && (SCSI_CURRENT_SENSING || SCSI_UPDATE_TERMINATION_ENABLE || SCSI_AUTO_TERMINATION)) */

/*********************************************************************
*
*  SCSIhGetCurrentSensing routine -
*
*     Read Termination Current Sensing status
*
*  Return Value:  0 - Termination Okay
*                 1 - Over Terminated
*                 2 - Under Terminated
*                 3 - Invalid Channel
*                  
*  Parameters:    hhcb
*                 channel_selection:
*                 0 - Primary Low
*                 1 - Primary High
*                 2 - Secondary Low
*                 3 - Secondary high
*
*  Remarks:       none
*                  
*********************************************************************/
#if SCSI_CURRENT_SENSING
SCSI_UEXACT8 SCSIhGetCurrentSensing (SCSI_HHCB SCSI_HPTR hhcb,
                                     SCSI_UEXACT8 channel_selection)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 current_sensing = 0;
   SCSI_UEXACT8 hcntrl;
   SCSI_UINT32 i = 0;          /* time out counter */
   SCSI_UEXACT8 chan_A = 0;

   /* check if pause chip necessary */
   if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);      
   }
   
   /* Mode 3 required */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
   /* Acquire Flexport */
   /* Request Write access to Harpoon flexport control reg 1*/
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDCTL),(SCSI_BRDEN | SCSI_FLXCSTREG1)); 
   
   /* Check for the gain access to Flexport */
   SCSI_hCHECKFLEXPORTACCESS(hhcb);
   
   /* activate current sensing */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDDAT),SCSI_CURRENT_SENSE_CTL); 
   
   /* Toggle strobe */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDCTL),
       (SCSI_BRDEN | SCSI_FLXCSTREG1 | SCSI_BRDSTB)); 
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDCTL),
       (SCSI_BRDEN | SCSI_FLXCSTREG1)); 
       
   /* Check for current sensing or HW status busy */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDCTL),
        (SCSI_BRDEN | SCSI_FLXCSTREG2 | SCSI_BRDRW| SCSI_BRDSTB)); 
   /* Wait for not busy*/     
   while (((OSD_INEXACT8(SCSI_AICREG(SCSI_BRDDAT)) & SCSI_FLXSTAT2_BUSY)) &&
          (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
   {
      OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
   }
   
   /* Toggle Strobe */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDCTL),
        (SCSI_BRDEN | SCSI_FLXCSTREG2 | SCSI_BRDRW)); 
        
   /* NOTE - STATUS 4 = CHAN A, STATUS 5 = CHAN B */    

   if (chan_A)
   {
      /* Request read access to Harpoon flexport status reg 4*/
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDCTL),
           (SCSI_BRDEN | SCSI_FLXCSTREG4 | SCSI_BRDRW | SCSI_BRDSTB));
   }
   else  /* CHAN B*/
   {     
      /* Request read access to Harpoon flexport status reg 5*/
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRDCTL),
           (SCSI_BRDEN | SCSI_FLXCSTREG5 | SCSI_BRDRW | SCSI_BRDSTB));
   }
        
   /* channel selection 0=Primary Low 1=Primary High 2=Secondary Low 3=Secondary High*/
   /*   SCSI_CSS_SECONDARY_HIGH1  0x80   CH X Current sensing bit         */
   /*   SCSI_CSS_SECONDARY_HIGH0  0x40   CH X current sensing bit         */
   /*   SCSI_CSS_SECONDARY_LOW1   0x20   CH X current sensing bit         */
   /*   SCSI_CSS_SECONDARY_LOW0   0x10   CH X current sensing bit         */
   /*   SCSI_CSS_PRIMARY_HIGH1    0x08   CH X current sensing bit         */
   /*   SCSI_CSS_PRIMARY_HIGH0    0x04   CH X current sensing bit         */
   /*   SCSI_CSS_PRIMARY_LOW1     0x02   CH X current sensing bit         */
   /*   SCSI_CSS_PRIMARY_LOW0     0x01   CH X current sensing bit         */

   /* read mode - Data shifted (channel select x 2) to set bits 0,1*/
   current_sensing = (OSD_INEXACT8(SCSI_AICREG(SCSI_BRDDAT)) >>
         (channel_selection*2)) & 03;
   
   /* deactivate current sensing */
   SCSIhWriteFlexRegister(hhcb,SCSI_FLXCSTREG1,00);
   
   /* unpause if originally unpaused */
   
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }

   return(current_sensing);
}
#endif /* SCSI_CURRENT_SENSING */




/*$Header: /vobs/u320chim/src/chim/hwm/hwmintr.c   /main/273   Tue Aug  5 15:49:48 2003   quan $*/
/***************************************************************************
*                                                                          *
* Copyright 1995-2003 Adaptec, Inc.  All Rights Reserved.                  *
*                                                                          *
* This software contains the valuable trade secrets of Adaptec or its      *
* licensors.  The software is protected under international copyright      *
* laws and treaties.  This software mahy only be used in accordance with   *
* the terms of its accompanying license agreement.                         *
*                                                                          *
***************************************************************************/

/***************************************************************************
*
*  Module Name:   HWMINTR.C
*
*  Description:
*                 Codes to implement interrupt dispatcher for hardware 
*                 management module
*
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         NONE
*
*  Entry Point(s):
*                 SCSIHFrontEndISR
*                 SCSIHBackEndISR
*
***************************************************************************/

#include "scsi.h"
#include "hwm.h"

/*********************************************************************
*
*  SCSIHFrontEndISR
*
*     Front end processing of ISR
*
*  Return Value:  One of 3 types of status.
*                 SCSI_NOTHING_PENDING        - no interrupt pending
*                 SCSI_INTERRUPT_PENDING      - interrupt pending
*                 SCSI_LONG_INTERRUPT_PENDING - long interrupt pending
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine will just save the interrupt status and
*                 then clear the interrupt was pending. It's OSM's
*                 responsibility to schedule the execution of
*                 SCSIHBackEndISR at proper time.
*
*********************************************************************/
#if ((!SCSI_STREAMLINE_QIOPATH) && (!SCSI_DOWNSHIFT_MODE))
SCSI_UINT8 SCSIHFrontEndISR (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hstintstat = 0;
   SCSI_UEXACT8 qoutcnt;
   int i;
      
   /* check if there is any command complete interrupt */
   for (i = 0;; i++)
   {
      qoutcnt = SCSIhQoutCount(hhcb);
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

         /* For Cardbus (APA-1480x), the intstat register value is 0xFF */
         /* when the card is hot removed from the bus.  For this case,  */
         /* CHIM returns with no interrupt pending.  This value is not  */
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
#if !SCSI_DCH_U320_MODE         
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRCMDINT);
#else         
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT),SCSI_CMDCMPLT);
#endif /* !SCSI_DCH_U320_MODE */
               /* go back to check after just clearng INT */
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
         return(SCSI_INTERRUPT_PENDING);
      }
      else
      {
         /* at least one exception interrupt pending */
         return(SCSI_LONG_INTERRUPT_PENDING);
      }
   }
   else
   {
      return(SCSI_NOTHING_PENDING);
   }
}
#endif /* ((!SCSI_STREAMLINE_QIOPATH) && (!SCSI_DOWNSHIFT_MODE)) */

/*********************************************************************
*
*  SCSIHFrontEndISR
*
*     Front end processing of ISR
*
*  Return Value:  One of 3 types of status.
*                 SCSI_NOTHING_PENDING        - no interrupt pending
*                 SCSI_INTERRUPT_PENDING      - interrupt pending
*                 SCSI_LONG_INTERRUPT_PENDING - long interrupt pending
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine will just save the interrupt status and
*                 then clear the interrupt was pending. It's OSM's
*                 responsibility to schedule the execution of
*                 SCSIHBackEndISR at proper time.
*
*********************************************************************/
#if ((!SCSI_STREAMLINE_QIOPATH) && SCSI_DOWNSHIFT_MODE)
SCSI_UINT8 SCSIHFrontEndISR (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hstintstat = 0;

#if SCSI_BIOS_SUPPORT
   SCSI_UEXACT8 bios_hstintstat = 0;
   SCSI_UEXACT8 bios_hcntrl;
#endif /* SCSI_BIOS_SUPPORT */
      
   /* just read interrupt status from hardware */
   hstintstat = OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT));

   /* For Cardbus (APA-1480x), the intstat register value is 0xFF */
   /* when the card is hot removed from the bus.  For this case,  */
   /* CHIM returns with no interrupt pending.  This value is not  */
   /* going to happen on the scsi host adapter. */
   if (hstintstat == 0xFF)
   {
#if SCSI_BIOS_SUPPORT
      bios_hstintstat = hstintstat = 0;
#else
      hhcb->SCSI_HP_hstintstat = hstintstat = 0;
#endif /* SCSI_BIOS_SUPPORT */
   }
   else
   {
      if (hstintstat & SCSI_CMDCMPLT)
      {
#if SCSI_BIOS_SUPPORT
         bios_hstintstat |= (hstintstat = SCSI_CMDCMPLT);
#else
         hhcb->SCSI_HP_hstintstat |= (hstintstat = SCSI_CMDCMPLT);
#endif /* SCSI_BIOS_SUPPORT */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRCMDINT);
      }

      if (hstintstat & SCSI_HSTINTSTAT_MASK)
      {
         /* keep interrupt status */
#if SCSI_BIOS_SUPPORT

         bios_hstintstat |= (hstintstat & (SCSI_HSTINTSTAT_MASK | SCSI_CMDCMPLT));
         bios_hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

#else /* !SCSI_BIOS_SUPPORT */

         hhcb->SCSI_HP_hstintstat |= (hstintstat &
                                      (SCSI_HSTINTSTAT_MASK | SCSI_CMDCMPLT));
         hhcb->SCSI_HP_hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

#endif /* SCSI_BIOS_SUPPORT */

         /* disable hardware interrupt with pause */
         SCSI_hWRITE_HCNTRL(hhcb, SCSI_PAUSE);
      }
      /* there is no point to record spurrious interrupt */
      hstintstat &= (SCSI_HSTINTSTAT_MASK | SCSI_CMDCMPLT);
   }

#if SCSI_BIOS_SUPPORT             
   SCSIHWriteScratchRam(hhcb,
                        (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_INTSTAT),
                        (SCSI_UEXACT16) 1,
                        (SCSI_UEXACT8 SCSI_IPTR) &bios_hstintstat);
   
   SCSIHWriteScratchRam(hhcb,
                        (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_HCNTRL),
                        (SCSI_UEXACT16) 1,
                        (SCSI_UEXACT8 SCSI_IPTR) &bios_hcntrl);
#endif /* SCSI_BIOS_SUPPORT */

   OSD_SYNCHRONIZE_IOS(hhcb);

   if (hstintstat)
   {
      /* if the interrupt is only command complete interrupt */
      if ((hstintstat & ~SCSI_CMDCMPLT) == 0)
      {
         /* then return normal interrupt pending */
         return(SCSI_INTERRUPT_PENDING);
      }
      else
      {
         /* at least one exception interrupt pending */
         return(SCSI_LONG_INTERRUPT_PENDING);
      }
   }
   else
   {
      return(SCSI_NOTHING_PENDING);
   }
}
#endif /* ((!SCSI_STREAMLINE_QIOPATH) && SCSI_DOWNSHIFT_MODE) */

/*********************************************************************
*
*  SCSIHBackEndISR
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
void SCSIHBackEndISR (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_UEXACT16 scbNumber;
   SCSI_UEXACT8 hstintstat;
   SCSI_UEXACT8 sstat0; 
   SCSI_UEXACT8 sstat1;
   SCSI_UEXACT8 simode0;
   SCSI_UEXACT8 arpintcode;
   SCSI_UEXACT8 exceptionCount;
   SCSI_UEXACT16 prgmcnt;
#if (!SCSI_DOWNSHIFT_MODE && !SCSI_DCH_U320_MODE)
   SCSI_UEXACT32 statCmd;
#endif /* (!SCSI_DOWNSHIFT_MODE && !SCSI_DCH_U320_MODE) */

#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
   SCSI_BOOLEAN pausePCIorPCIXFlag = SCSI_FALSE;
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */

#if SCSI_ASYNC_NARROW_MODE
   SCSI_UEXACT8 scsidatl;
#endif /* SCSI_ASYNC_NARROW_MODE */

#if SCSI_BIOS_SUPPORT
   SCSI_UEXACT8 bios_hstintstat = 0;
   SCSI_UEXACT8 bios_hcntrl;   
   SCSI_UEXACT8 bios_originalmode;    
#endif /* SCSI_BIOS_SUPPORT */

#if SCSI_BIOS_SUPPORT
   /* Saved front end status */
   SCSIHReadScratchRam(hhcb,
                       (SCSI_UEXACT8 SCSI_SPTR) &hstintstat,
                       (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_INTSTAT),
                       (SCSI_UEXACT16) 1);
                          
#else /* !SCSI_BIOS_SUPPORT */

   hstintstat = hhcb->SCSI_HP_hstintstat;

#endif /* SCSI_BIOS_SUPPORT */

   if (hstintstat & SCSI_CMDCMPLT)
   {
      /* handle command complete interrupt */
      SCSIhCmdComplete(hhcb);
   }

   /* We need to read the hstintstat from the HHCB structure again because   */
   /* it may have been updated in the SCSIhCmdComplete() routine as a part of*/
   /* the workaround for the Harpoon2 Rev B hardware problem (razor issue#   */
   /* 904) wherein interrupts might get lost when a clear cmdcmpt int is done*/
   /* in the SCSIhCmdComplete() routine. */
   
#if SCSI_BIOS_SUPPORT
   /* Saved front end status */
   SCSIHReadScratchRam(hhcb,
                       (SCSI_UEXACT8 SCSI_SPTR) &hstintstat,
                       (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_INTSTAT),
                       (SCSI_UEXACT16) 1);
   /* Clear status */
   SCSIHWriteScratchRam(hhcb,
                        (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_INTSTAT),
                        (SCSI_UEXACT16) 1,
                        (SCSI_UEXACT8 SCSI_IPTR) &bios_hstintstat);
                          
#else /* !SCSI_BIOS_SUPPORT */

   hstintstat = hhcb->SCSI_HP_hstintstat;
   hhcb->SCSI_HP_hstintstat = 0;

#endif /* SCSI_BIOS_SUPPORT */

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
#if !SCSI_DOWNSHIFT_MODE
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
         statCmd = OSD_READ_PCICFG_EXACT32(hhcb,SCSI_STATUS_CMD_REG);

         OSD_WRITE_PCICFG_EXACT32(hhcb,SCSI_STATUS_CMD_REG, (statCmd & (~SCSI_SERRESPEN | ~SCSI_PERRESPEN)));
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

         OSD_WRITE_PCICFG_EXACT32(hhcb,SCSI_STATUS_CMD_REG, statCmd);
      }

      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID81);
      hstintstat = 0;

      /* unpause chip (restore interrupt enable bit) */
      SCSI_hWRITE_HCNTRL(hhcb, (SCSI_UEXACT8)(hhcb->SCSI_HP_hcntrl & SCSI_INTEN));

#endif /* !SCSI_DCH_U320_MODE */
   }
#endif /* !SCSI_DOWNSHIFT_MODE */

   /* check if there was any exceptional interrupt */
   if (hstintstat & SCSI_HSTINTSTAT_MASK)
   {
#if SCSI_BIOS_SUPPORT
      /* Save the original mode */ 
      bios_originalmode = SCSI_hGETMODEPTR(hhcb,scsiRegister);   
   
      SCSIHWriteScratchRam(hhcb,
                           (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ORIGINALMODE),
                           (SCSI_UEXACT16) 1,
                           (SCSI_UEXACT8 SCSI_IPTR) &bios_originalmode);

#else /* !SCSI_BIOS_SUPPORT */

      /* Save the original mode */ 
      hhcb->SCSI_HP_originalMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

#endif /* SCSI_BIOS_SUPPORT */

      exceptionCount = 0;

      while ((hstintstat |= OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT))) &
                              SCSI_HSTINTSTAT_MASK)
      {
         /* Set MODE 3 */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

#if !SCSI_BIOS_SUPPORT
         /* Clear the hstintstat indicator. */
         hhcb->SCSI_HP_hstIntsCleared = 0;
#endif /* !SCSI_BIOS_SUPPORT */
         
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
            
            /* If the OSM has chosen to issue the SCSI_CMD_RESET_HARDWARE
             * HIOB during the PCI error handler the Sequencer can be
             * unpaused. 
             */
            if (hhcb->SCSI_HP_hstintstat & SCSI_PCIINT) 
            {
               /* Do not attempt to Unpause the sequencer
                * due to extended cleanup required by the
                * OSM has not completed as of this time.
                */
                pausePCIorPCIXFlag = SCSI_TRUE;
            }   
            hhcb->SCSI_HP_hstintstat = 0; /* clear all ints status */
            break;  
         }
         /* handle Split interrupts (PCI-X) */
         if (hstintstat & SCSI_SPLTINT)
         {
            /* save hstintstat for proper recovery */
            hhcb->SCSI_HP_hstintstat = hstintstat;
            hstintstat &= ~SCSI_SPLTINT;
            hhcb->SCSI_HP_inISRContext = 1;
            SCSIhHandleSplitInterrupt(hhcb);
            /* clear flag for hardware reset if yet to be done */
            hhcb->SCSI_HP_inISRContext = 0;
            
            /* If the OSM has chosen to issue the SCSI_CMD_RESET_HARDWARE
             * HIOB during the PCI error handler the Sequencer can be
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

            /* Use the CLRHSTINT to clear SPLTINT */
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

#if SCSI_DOWNSHIFT_MODE
            if ((SCSI_hHARPOON_REV_B(hhcb)) && (SCSI_hARPINTVALID_REG(hhcb) != SCSI_NULL_ENTRY))
#else      
            if (SCSI_hARPINTVALID_REG(hhcb) != SCSI_NULL_ENTRY)
#endif /* SCSI_DOWNSHIFT_MODE */
            {
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_hARPINTVALID_REG(hhcb)),
                  OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_hARPINTVALID_REG(hhcb))) &
                  ~(SCSI_hARPINTVALID_BIT(hhcb)));

               /* Increment the value in the CURADDR registers, and then */
              /* write this value into the PRGMCNT registers so that it will skip */
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

            SCSI_hHANDLEHWERRINTERRUPT(hhcb);
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

         if ((sstat1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1))) & SCSI_SCSIRSTI)
         {
            /* scsi reset interrupt */
            SCSIhIntSrst(hhcb);
            SCSI_SET_NULL_SCB(scbNumber);
            /* Clear local hstintstat variable */
            hstintstat = 0;
            continue;
         }
         else
         if (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & SCSI_IOERR)
         {
            /* IO Operating mode change */
#if (!SCSI_ASPI_SUPPORT || !SCSI_DOWNSHIFT_MODE)
            SCSIhIntIOError(hhcb);
            SCSI_SET_NULL_SCB(scbNumber);
            /* Clear local hstintstat variable */
            hstintstat = 0;
            continue;
#else
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID82);
            break;
#endif /* !SCSI_ASPI_SUPPORT || !SCSI_DOWNSHIFT_MODE */
         }
         else
         {
            if (sstat1 & SCSI_SELTIMO)    /* handle SCSI selection timeout */
            {
               scbNumber = ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
                            (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));
            }
            else
            {
#if SCSI_BIOS_SUPPORT
               SCSI_hSETMODEPTR(hhcb,scsiRegister,bios_originalmode);
#else /* !SCSI_BIOS_SUPPORT */
               SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);
#endif /* SCSI_BIOS_SUPPORT */

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
#if SCSI_STANDARD_MODE
            hiob = SCSI_ACTPTR[scbNumber];
#elif !SCSI_BIOS_SUPPORT
            hiob = SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET];
#else
            SCSIHReadScratchRam(hhcb,
                                (SCSI_UEXACT8 SCSI_SPTR) &hiob,
                                (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ACTIVE_HIOB),
                                (SCSI_UEXACT16) 4);
#endif /* SCSI_STANDARD_MODE */
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

#if SCSI_DOWNSHIFT_MODE
            if ((SCSI_hHARPOON_REV_B(hhcb)) && (SCSI_hARPINTVALID_REG(hhcb) != SCSI_NULL_ENTRY))
#else      
            if (SCSI_hARPINTVALID_REG(hhcb) != SCSI_NULL_ENTRY)
#endif /* SCSI_DOWNSHIFT_MODE */

            {
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_hARPINTVALID_REG(hhcb)),
                  OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_hARPINTVALID_REG(hhcb))) &
                  ~(SCSI_hARPINTVALID_BIT(hhcb)));

               /* Increment the value in the CURADDR registers, and then */
               /* write this value into the PRGMCNT registers so that it will skip */
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

            arpintcode = OSD_INEXACT8(SCSI_AICREG(SCSI_ARPINTCODE));
            if ((SCSI_IS_NULL_SCB(scbNumber)) || (hiob == SCSI_NULL_HIOB))
            {
               /* If hiob is SCSI_NULL_HIOB then don't enter any routine     */
               /* which doesn't have protection on hiob parameter.           */
               /* Abort only if it is not a break point interrupt at idle    */
               /* loop, as an scb can be null in this case                   */
#if SCSI_DOMAIN_VALIDATION
               if ((arpintcode != SCSI_IDLE_LOOP_BREAK) && (arpintcode != SCSI_DV_TIMEOUT))
#else
               if (arpintcode != SCSI_IDLE_LOOP_BREAK)
#endif /*SCSI_DOMAIN_VALIDATION*/
               {
                  arpintcode = SCSI_ABORT_TARGET;
               }
            }

            /* process all the sequencer interrupts */
            switch (arpintcode)
            {
               case SCSI_DATA_OVERRUN:         /* data overrun/underrun */
               case SCSI_DATA_OVERRUN_BUSFREE:
                  SCSIhCheckLength(hhcb,hiob);
                  break;

               case SCSI_CDB_XFER_PROBLEM:     /* cdb bad transfer */
                  SCSI_hCDBABORT(hhcb,hiob);
                  break;

               case SCSI_HANDLE_MSG_OUT:       /* send msg out */
                  SCSIhHandleMsgOut(hhcb,hiob);
                  break;

               case SCSI_SYNC_NEGO_NEEDED:
#if !SCSI_ASYNC_NARROW_MODE
                  SCSI_hNEGOTIATE(hhcb,hiob);
#else /* SCSI_ASYNC_NARROW_MODE */

                  /* set reentrant address for sequencer */
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                      (SCSI_UEXACT8) (SCSI_hSIOSTR3_ENTRY(hhcb) >> 2));
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                      (SCSI_UEXACT8) (SCSI_hSIOSTR3_ENTRY(hhcb) >> 10));

                  /* deassserted ATN and transfer the last byte */
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);
                  scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));

#endif /* !SCSI_ASYNC_NARROW_MODE */

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

#if !SCSI_DOWNSHIFT_MODE
#if SCSI_PACKETIZED_IO_SUPPORT
                  /* There is no need to handle special_function, in this */
                  /* case is target reset, in a packetized mode.          */
                  if (!SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))
                  {
                     SCSI_hENQUEHEADTR(hhcb, hiob);
                  }
#else /* !SCSI_PACKETIZED_IO_SUPPORT */
                  SCSI_hENQUEHEADTR(hhcb, hiob);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
                  break;
#endif /* !SCSI_DOWNSHIFT_MODE */
#if SCSI_EFI_BIOS_SUPPORT
                  SCSIhTerminateCommand(hiob);
#endif /* SCSI_EFI_BIOS_SUPPORT */
                  break;
#if SCSI_SEQBREAKPOINT_WORKAROUND
               case SCSI_IDLE_LOOP_BREAK:
               case SCSI_EXPANDER_BREAK:
                  SCSI_hBREAKINTERRUPT(hhcb,hiob);
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
                   * assume something wrong
                   */
                  SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID02);
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

#if SCSI_TARGET_OPERATION  
            if ((sstat0 & SCSI_TARGET) && (sstat1 & SCSI_ATNTARG) &&
                (OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE1)) & SCSI_ENATNTARG))
            {
               /* Should not come through this path any longer. */
               /* Get active HIOB from the current fifo pointer. */
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
#if (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)
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
                SCSI_hHARPOON_REV_A(hhcb))
            {
               /* Harpoon2 Rev A chip has the following problem in the I/O   */
               /* cell:                                                      */
               /*    When the SCSI bus is in LVD mode the I/O cell sampling  */
               /*    module does not get initialized properly.  The          */
               /*    workaround is to enable the bypass after a power-on chip*/
               /*    reset and on an IOERR interrupt and disable the bypass  */
               /*    after the first selection done or selection timeout. The*/
               /*    details of this hardware problem can be found in the    */
               /*    Razor database entry #491.                              */
               /* Here we check if selection done interrupt has occurred.  If*/
               /* so we disable the bypass and also the selection done       */
               /* interrupt enable so that no more selection done interrupts */
               /* will occur.                                                */
               /* If operating in target mode then a selection-in may occur  */
               /* first. Therefore, similar logic is applied to ENSELDI      */
               /* interrupt.                                                 */ 
               /* Note that we can assume that bus is in LVD mode, because   */
               /* the ENSELDO or ENSELDI would be set only if that is true.  */

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

               OSD_OUTEXACT8(SCSI_AICREG(SCSI_DSPDATACTL),
                   OSD_INEXACT8(SCSI_AICREG(SCSI_DSPDATACTL)) &
                                  ~SCSI_BYPASSENAB);

               SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);             
            }
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE) */
#if SCSI_PACKETIZED_IO_SUPPORT
#if SCSI_INITIATOR_OPERATION
            else
            if (OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT1)) &
                    (SCSI_LQIPHASE1 | SCSI_LQIPHASE2 | SCSI_LQIOVERI1))
            {
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
#if !SCSI_ASPI_REVA_SUPPORT   
               if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
               {
                  /* Harpoon2 Rev A0, A1 and A2 chips can fall into a     */
                  /* wherein, if the target is following all the rules,   */
                  /* the SCSI bus can be hung with the last ACK of the LQ */
                  /* packet pending.                                      */
                  SCSIhLQIAbortOrLQIBadLQI(hhcb,hiob);
               }
#else /* SCSI_ASPI_REVA_SUPPORT */
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRLQIINT1),
                   (SCSI_CLRLQIABORT | SCSI_CLRLQIBADLQI));
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), SCSI_CLRSCSIINT);

               SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID69);
#endif /* !SCSI_ASPI_REVA_SUPPORT */   
            }
            else
            if (OSD_INEXACT8(SCSI_AICREG(SCSI_LQOSTAT1)) & SCSI_LQOPHACHG1)
            {
#if !SCSI_ASPI_REVA_SUPPORT
               if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
               {
                  /* Harpoon2 Rev A0, A1 and A2 chips can fall into a        */
                  /* wherein, if the target is following all the rules,      */
                  /* the LQO manager can get hung due to an exception */
                  SCSIhLQOErrors(hhcb,hiob);
               }
#else /* SCSI_ASPI_REVA_SUPPORT */             
   
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), SCSI_CLRSCSIINT);
               SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID70);
   
#endif /* !SCSI_ASPI_REVA_SUPPORT */
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

#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
         /* process sequencer breakpoint */
         else if (hstintstat & SCSI_BRKADRINT)
         {
            hstintstat &= ~SCSI_BRKADRINT;

            /* interrupt might be for expander active, active abort */
            /* or target reset. */
            SCSI_hBREAKINTERRUPT(hhcb,hiob);

            /* clear break point address interrupt */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRBRKADRINT);
         }
#endif /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)*/

#if !SCSI_BIOS_SUPPORT
         if (hhcb->SCSI_HP_hstIntsCleared)
         {
            /* Interrupts were cleared by call such as SCSIhBadSeq, 
             * therefore clear the local hstintstat variable.
             */
            hstintstat = 0;
         }
#endif /* !SCSI_BIOS_SUPPORT */

      } /* end of while loop */

#if SCSI_BIOS_SUPPORT
      /* restore original MODE value */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,bios_originalmode);
               
      SCSIHReadScratchRam(hhcb,
                          (SCSI_UEXACT8 SCSI_SPTR) &bios_hcntrl,
                          (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_HCNTRL),
                          (SCSI_UEXACT16) 1);

      /* unpause chip (restore interrupt enable bit) */
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
      /* For PCI/PCIX error handling, we will not unpause the sequencer */
      /* if the SCSI_CMD_RESET_HARDWARE HIOB has not been issued yet.   */
      if (pausePCIorPCIXFlag == SCSI_FALSE)
      {
         SCSI_hWRITE_HCNTRL(hhcb,(SCSI_UEXACT8)(bios_hcntrl & SCSI_INTEN));
      }
#else      
      SCSI_hWRITE_HCNTRL(hhcb,(SCSI_UEXACT8)(bios_hcntrl & SCSI_INTEN));
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */

#else /* !SCSI_BIOS_SUPPORT */

      /* restore original MODE value */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);

#if !SCSI_DCH_U320_MODE 
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
      /* For PCI/PCIX error handling, we will not unpause the sequencer */
      /* if the SCSI_CMD_RESET_HARDWARE HIOB has not been issued yet.   */
      if (pausePCIorPCIXFlag == SCSI_FALSE)
      {
         SCSI_hWRITE_HCNTRL(hhcb,(SCSI_UEXACT8)(hhcb->SCSI_HP_hcntrl & SCSI_INTEN));
      }
#else      
      /* unpause chip (restore interrupt enable bit) */
      SCSI_hWRITE_HCNTRL(hhcb,(SCSI_UEXACT8)(hhcb->SCSI_HP_hcntrl & SCSI_INTEN));
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */

#else   
      /* DCH does not support interrupt enable bit */   
      SCSI_hWRITE_HCNTRL(hhcb, (SCSI_UEXACT8)(hhcb->SCSI_HP_hcntrl & ~(SCSI_PAUSE | SCSI_PAUSEREQ)));

#endif /* !SCSI_DCH_U320_MODE */
#endif /* SCSI_BIOS_SUPPORT */
   }
          
   OSD_SYNCHRONIZE_IOS(hhcb);
}
#endif /* !SCSI_STREAMLINE_QIOPATH */

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
/* Modules internal to hardware management layer                          */
/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

/*********************************************************************
*
*  SCSIhCmdComplete
*
*     Handle command complete interrupt from sequencer
*
*  Return Value:  number of complete scb processed at all
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine should be referenced by interrupt dispatcher.
*                 At entrance of interrupt dispatcher it should always
*                 assume the interrupt pending is command complete.
*                 Without verifying the source of interrupt this routine
*                 must be called blindly. The return status of this routine
*                 indicates if there was a command complete pending or not.
*                 The interrupt dispatcher should check the return status
*                 of this routine. If the returned status is not zero then
*                 interrupt dispatcher should return control to resource
*                 management layer (with return statement). If the returned
*                 status is zero then interrupt dispatcher should verify
*                 if there is any error/exception interrupt pending and
*                 process it until all pending error/exception has been
*                 processed.
*
*                 If there was any command complete pending it will be
*                 cleared by this routine.
*
*********************************************************************/
#if !SCSI_STREAMLINE_QIOPATH
int SCSIhCmdComplete (SCSI_HHCB SCSI_HPTR hhcb)
{
   int retValue = 0;
   SCSI_HIOB SCSI_IPTR hiob;
#if !SCSI_BIOS_SUPPORT
   SCSI_UEXACT16 scbNumber = 0;
#endif /* !SCSI_BIOS_SUPPORT */
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
#if !SCSI_DOWNSHIFT_MODE
   SCSI_UEXACT8 arpintcode;
#endif /*!SCSI_DOWNSHIFT_MODE*/
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 saved_hcntrl = 0;
#if !SCSI_DCH_U320_MODE
   SCSI_UEXACT32 statCmd;
#endif /* !SCSI_DCH_U320_MODE */

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
      
#if !SCSI_DOWNSHIFT_MODE
      /* service all command complete outstanding */
      while (!SCSI_IS_NULL_SCB(scbNumber = SCSIhRetrieveScb(hhcb)))
#endif /* !SCSI_DOWNSHIFT_MODE */
      {

#if SCSI_STANDARD_MODE
         hiob = SCSI_ACTPTR[scbNumber];
            
#elif !SCSI_BIOS_SUPPORT
         hiob = SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET];
            
#else 
         SCSIHReadScratchRam(hhcb,
                             (SCSI_UEXACT8 SCSI_SPTR) &hiob,
                             (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ACTIVE_HIOB),
                             (SCSI_UEXACT16) 4);
                 
#endif /* SCSI_STANDARD_MODE */

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
                  statCmd = OSD_READ_PCICFG_EXACT32(hhcb,SCSI_STATUS_CMD_REG);

                  OSD_WRITE_PCICFG_EXACT32(hhcb,SCSI_STATUS_CMD_REG, (statCmd & (~SCSI_SERRESPEN | ~SCSI_PERRESPEN)));
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

                  OSD_WRITE_PCICFG_EXACT32(hhcb,SCSI_STATUS_CMD_REG, statCmd);
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
            ++retValue;

            /* terminate the command */
            SCSI_hTERMINATECOMMAND(hiob);
         }

#if !SCSI_DOWNSHIFT_MODE
         --hhcb->SCSI_HP_qoutcnt;
#else
         break;
#endif /* !SCSI_DOWNSHIFT_MODE */

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
            return(retValue);
         }
#endif /* SCSI_BACKENDISR_OUTSIDE_INTRCONTEXT */
      }

#if !SCSI_DOWNSHIFT_MODE
 
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
#endif /* !SCSI_DOWNSHIFT_MODE */

   }

#if !SCSI_DOWNSHIFT_MODE
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
#if !SCSI_ASPI_REVA_SUPPORT
   if ((saved_hcntrl & SCSI_PAUSEACK) || (OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & SCSI_PAUSEACK))
   {
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
                  hhcb->SCSI_HP_hstintstat |= SCSI_HWERRINT;
               }
               else
               {
                  /* Case 1.... */
                  hhcb->SCSI_HP_hstintstat |= SCSI_ARPINT;
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
            hhcb->SCSI_HP_hstintstat |= SCSI_BRKADRINT;
         }
      }
   }
#endif /* !SCSI_ASPI_REVA_SUPPORT */
#endif /*!SCSI_DOWNSHIFT_MODE*/

   return(retValue);
}
#endif /* !SCSI_STREAMLINE_QIOPATH */

/*********************************************************************
*
*  SCSIhIntClrDelay routine -
*
*     Handle reset in and IO error clear interrupt and delay 
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
void SCSIhIntClrDelay (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UINT32 j = 0;          /* time out counter */
   SCSI_UINT32 i = 0;

   /* convert resetDelay from msec to 500usec and add 1 to ensure that we 
    * enter loop at least once
    */
   SCSI_UINT32 timerCount = ((SCSI_UINT32)hhcb->resetDelay * 2) + 1;
   
   for (j = 0; j < timerCount; j++)
   {
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT)) &
          (SCSI_BRKADRINT | SCSI_SCSIINT | SCSI_ARPINT))
      {
         /* wait until reset deasserted */
         i=0;           /*watchdog loop counter for reset gone*/
         while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & SCSI_SCSIRSTI) &&
            (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRSCSIRSTI);
            OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
         }
         
         /*If reset never goes away then do not reset j loop  */
         if (i < SCSI_REGISTER_TIMEOUT_COUNTER)
         {
            j=0;              /*Restart timer count if reset de-asserted*/
         }
         else if (j==1)
         {
            j=timerCount;     /*Stuck for 2 register timeouts then quit.*/
         }
         
         /* Reset channel hardware */
         SCSIhResetChannelHardware(hhcb);
       
         /* Reset channel software's parameters */
         SCSI_hRESETSOFTWARE(hhcb);
      }

      /* Wait for 500 uSec each time */
      SCSIhDelayCount25us(hhcb, SCSI_hGETTIMERCOUNT(hhcb, 500));
   }   
#if !SCSI_BIOS_SUPPORT
   /* Set here for the PAC to invoke suppress negotiation.
    * This to make sure the internal Bus Scan always communicates
    * async/narrow to devices.
    */
   hhcb->SCSI_HF_resetSCSI = 1;

   for (i = 0; i < SCSI_MAXDEV; i++)
   {
      SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DF_resetSCSI = 1;
   }
#endif /* !SCSI_BIOS_SUPPORT */
}
/*********************************************************************
*
*  SCSIhIntSrst routine -
*
*     Handle cases when another device resets scsi bus
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
void SCSIhIntSrst (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UINT32 j = 0;          /* time out counter */

   /* Set mode 3. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* wait until reset deasserted */
   while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & SCSI_SCSIRSTI) &&
          (j++ < SCSI_REGISTER_TIMEOUT_COUNTER))
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRSCSIRSTI);
      OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
   }

   /* Set event reason field */
   hhcb->freezeEventReason = SCSI_AE_3PTY_RESET;
   SCSI_ASYNC_EVENT(hhcb, SCSI_AE_OSMFREEZE);      /* AEN call to RSM layer */

   /* Disable data channels */
   SCSIhDisableBothDataChannels(hhcb);
   /* SCSISEQ0/1 registers have been modified but are restored
    * in SCSIhResetChannelHardware.
    */

#if SCSI_TARGET_OPERATION
   /* If  target mode enabled need to reset the holding
    * SCSI bus indicator or SCSIhTargetClearNexus will fail
    */ 
   if (hhcb->SCSI_HF_targetMode)
   {    
      if (hhcb->SCSI_HF_targetScsiBusHeld)
      {
         hhcb->SCSI_HF_targetScsiBusHeld = 0;
         hhcb->SCSI_HP_nexusHoldingBus->SCSI_XF_busHeld = 0;
      }

      /* Block upper layers from issuing Establish Connection HIOBs */
      SCSI_hTARGETHOLDESTHIOBS(hhcb,1);
   }
#endif /* SCSI_TARGET_OPERATION */

   if (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO)
   {
      /* If ENSELO is enabled the hardware will issue a 
       * selection or reselection (target mode) after the reset -
       * this is a harware bug.
       * The workaround is to check if SELINGO or SELDO is set and
       * issue a reset to clear the false selection. Otherwise we
       * could delay in SCSIhBusWasDead if a device responded to
       * the selection.
       */
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & (SCSI_SELDO | SCSI_SELINGO))
      {
         SCSIhResetScsi(hhcb);
      }
   }

   SCSIhResetChannelHardware(hhcb);                 /* reset channel hardware*/
   SCSI_hABORTCHANNEL(hhcb, SCSI_HOST_ABT_3RD_RST); /* abort all active cmds */
   SCSI_hRESETSOFTWARE(hhcb);                       /* reset channel software*/

#if SCSI_TEST_OSMEVENTS
   /* If softwareVersion is not correct then this is a */ 
   /* request to simulate a SCSI_AE_IO_CHANNEL_FAILED  */
   if (SCSIhBusWasDead(hhcb) ||
       (hhcb->softwareVersion != SCSI_SOFTWARE_VERSION))
   {                                               /* dead, then do reset   */
      if (SCSIhResetScsi(hhcb) ||
          (hhcb->softwareVersion != SCSI_SOFTWARE_VERSION))
      {  
         SCSI_hSETBUSHUNG(hhcb,1);
         /* Restore softwareVersion */ 
         hhcb->softwareVersion = SCSI_SOFTWARE_VERSION;
         SCSIhResetChannelHardware(hhcb);          /* reset channel hardware */
         SCSI_hRESETSOFTWARE(hhcb);                /* reset channel software */
      }
   }
#else /* !SCSI_TEST_OSMEVENTS */
   if (SCSIhBusWasDead(hhcb))                      /* check if bus is really */
   {                                               /* dead, then do reset    */
      if (SCSIhResetScsi(hhcb))
      {
         SCSI_hSETBUSHUNG(hhcb,1);
         SCSIhResetChannelHardware(hhcb);          /* reset channel hardware */
         SCSI_hRESETSOFTWARE(hhcb);                /* reset channel software */
      }
   }
#endif /* SCSI_TEST_OSMEVENTS */

   if (!hhcb->SCSI_HF_busHung)
   {
      /* clear interrupt and delay */
      SCSIhIntClrDelay(hhcb);
   }
   
   /* clear interrupt and delay */
   SCSIhIntClrDelay(hhcb);
   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0), 
                 (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1), 
                 (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));

   /* Unblock upper layers from issuing Establish Connection HIOBs */
   SCSI_hTARGETHOLDESTHIOBS(hhcb,0);

#if (SCSI_TRIDENT_WORKAROUND && SCSI_TARGET_OPERATION)
   /* Do not remove this code until we verify that it's not
    * required for U320 ASIC.
    */
   /* In Trident after the reset the sequencer does not turn
    * off the SPIOEN, which causes the chip to assert a spurious
    * 'REQ' during the selection phase if it responds as a target
    * after the reset.  This workaround will prevent that from
    * happening.
    */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL0),
                 (OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL0)) & ~SCSI_SPIOEN));
#endif /* (SCSI_TRIDENT_WORKAROUND && SCSI_TARGET_OPERATION) */

   OSD_SYNCHRONIZE_IOS(hhcb);

#if !SCSI_BIOS_SUPPORT
      if (hhcb->SCSI_HF_busHung && 
              OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & SCSI_SCSIRSTI)
      {
         /* Async. Event to OSM */
         SCSI_ASYNC_EVENT(hhcb, SCSI_AE_3PTY_RESET);
         /*reset it back for ISR!*/
         SCSI_hSETBUSHUNG(hhcb,1);
      }
      else
#endif /*!SCSI_BIOS_SUPPORT*/
      {
         /* Async. Event to OSM */
         SCSI_ASYNC_EVENT(hhcb, SCSI_AE_3PTY_RESET);
   
      }

   SCSI_ASYNC_EVENT(hhcb, SCSI_AE_OSMUNFREEZE); /* AEN call back to RSM layer */
}

/*********************************************************************
*
*  SCSIhIntIOError routine -
*
*     Handle cases when the I/O operating mode is changed
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*
*  Remarks:       
*                  
*********************************************************************/
#if (!SCSI_ASPI_SUPPORT || !SCSI_DOWNSHIFT_MODE)
void SCSIhIntIOError (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

#if (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)
   SCSI_UEXACT8 simode0;
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE) */

   /* Set mode 3. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT0), SCSI_CLRIOERR);

   /* Set event reason field */
   hhcb->freezeEventReason = SCSI_AE_IOERROR;
   SCSI_ASYNC_EVENT(hhcb, SCSI_AE_OSMFREEZE);      /* AEN call to RSM layer */

   /* Disable data channels */
   SCSIhDisableBothDataChannels(hhcb);
   /* SCSISEQ0/1 registers have been modified but are restored
    * in SCSIhResetChannelHardware.
    */

#if SCSI_TARGET_OPERATION
   if (hhcb->SCSI_HF_initiatorMode)
   {
      /* We need to reset SCSI bus after ioerror happens */
      /* This is a violation of SPI-2 specification - we */
      /* will only do this if initiator mode enabled.    */ 
      if (SCSIhResetScsi(hhcb))
      {
         SCSI_hSETBUSHUNG(hhcb,1);
      }
   }
#else
   /* We need to reset SCSI bus after ioerror happens */
   /* This is a violation of SPI-2 specification - we */
   /* will only do this if initiator mode enabled.    */ 
   if (SCSIhResetScsi(hhcb))
   {
      SCSI_hSETBUSHUNG(hhcb,1);
      
   }
#endif /* SCSI_TARGET_OPERATION */

   SCSIhResetChannelHardware(hhcb);                /* reset channel hardware*/
    /* Block upper layers from issuing Establish Connection HIOBs */
   SCSI_hTARGETHOLDESTHIOBS(hhcb,1);
   SCSI_hABORTCHANNEL(hhcb, SCSI_HOST_ABT_IOERR);   /* abort all active cmds */
   SCSI_hRESETSOFTWARE(hhcb);                      /* reset channel software*/

   /* clear interrupt and delay */
   SCSIhIntClrDelay(hhcb);
   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0), 
         (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1), 
         (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));

   /* Unblock upper layers from issuing Establish Connection HIOBs */
   SCSI_hTARGETHOLDESTHIOBS(hhcb,0);   

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

      /* Restore back to mode 3 */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   }

   OSD_SYNCHRONIZE_IOS(hhcb);

   /* Async. Event to OSM */
   SCSI_ASYNC_EVENT(hhcb, SCSI_AE_IOERROR);

   SCSI_ASYNC_EVENT(hhcb, SCSI_AE_OSMUNFREEZE); /* AEN call back to RSM layer */

}
#endif /* !SCSI_ASPI_SUPPORT || !SCSI_DOWNSHIFT_MODE */
/*********************************************************************
*
*  SCSIhCheckLength
*
*     Check for underrun/overrun conditions following exception
*     condition occuring during data transfer
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Incoming Mode : Don't Care         
*                  
*********************************************************************/
void SCSIhCheckLength (SCSI_HHCB SCSI_HPTR hhcb,
                       SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 ubyteWork;
   SCSI_UEXACT8 stat;
   SCSI_UINT32 i;             /* time out counter */
   SCSI_UEXACT32 residualLength;
   SCSI_UEXACT8 modeSave;
   SCSI_UEXACT8 originalMode;

   /* Save the incoming mode, and put in the mode that we got the interrupt, */
   /* which should be either 0, or 1 */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

#if SCSI_BIOS_SUPPORT
   SCSIHReadScratchRam(hhcb,
                       (SCSI_UEXACT8 SCSI_SPTR) &originalMode,
                       (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ORIGINALMODE),
                       (SCSI_UEXACT16) 1);
#else /* !SCSI_BIOS_SUPPORT */
   originalMode = hhcb->SCSI_HP_originalMode;
#endif /* SCSI_BIOS_SUPPORT */

   SCSI_hSETMODEPTR(hhcb,scsiRegister,originalMode);

#if !SCSI_DOWNSHIFT_MODE
   if (hiob->SCSI_IF_freezeOnError &&
       !(SCSI_TARGET_UNIT(hiob)->targetControl->freezeMap &
         SCSI_FREEZE_HIPRIEXISTS))
   {
      SCSI_hFREEZEHWQUEUE(hhcb,hiob,SCSI_AE_FREEZEONERROR_START);
   }
#endif /* !SCSI_DOWNSHIFT_MODE */

   /* Determine if an underrun or an overrun has occured. */
   if (SCSI_hUNDERRUN(hhcb))
   {
      /* Underrun has happened.  Calculate the residual bytes. */

      /* Do nothing if underrun error suppressed. */
      if (hiob->SCSI_IF_noUnderrun)
      {
         return;
      }
      else
      {
         /* Calculate the number of bytes that were not transferred. */
         residualLength = SCSI_hRESIDUECALC(hhcb,hiob);

#if (SCSI_PACKETIZED_IO_SUPPORT)
         /* Check if the underrun happened during a packetized transfer. */
         if (SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))
         {
            /* Underrun during packetized transfer...                        */
            /* Since the sequencer transfers all the status IU bytes that is */
            /* received from the target, an overrun or underrun should never */
            /* happen during the status IU transfer.  So, we have to assume  */
            /* here that the underrun has happened during the regular data   */
            /* transfer. */
            hiob->residualLength = residualLength;
         }
         else
#endif /* (SCSI_PACKETIZED_IO_SUPPORT) */
         {
            /* Underrun during non-packetized transfer...                    */
            /* Report it in the appropriate IOB field depending on whether   */
            /* the underrun happened during a regular I/O or a Request Sense */
            /* I/O. */
            if (hiob->trgStatus == SCSI_UNIT_CHECK)
            {
               hiob->snsResidual = (SCSI_UEXACT8) residualLength;
               return ;
            }
            else
            {
               hiob->residualLength = residualLength;
            }
         }
      }
   }
   else
   {
      /* Overrun has happened.  Bit-bucket the extra bytes. */

      /* Since Harpoon2 Rev B chip supports bit-bucketting in both the      */
      /* packetized and non-packetized mode, we need to handle accordingly. */

/* $$ DCH $$ .. need to add check for DCH if applicable */
#if (!SCSI_DOWNSHIFT_U320_MODE  && !SCSI_ASPI_REVA_SUPPORT)
      if (SCSI_hHARPOON_REV_B(hhcb) || SCSI_hDCH_SCSI_CORE(hhcb))
      {
         /* Turn on the bit bucketting. */
         ubyteWork = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL),
            ubyteWork | SCSI_M01BITBUCKET);
         
         /* The hardware automatically turns it off when the bit bucketting */
         /* is completed. Simply wait for the event. */
         i = 0;
         while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL)) &
                   SCSI_M01BITBUCKET) && (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
         {
            OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
         }

         if (i >= SCSI_REGISTER_TIMEOUT_COUNTER)
         {
            /* Terminate the I/O with an appropriate error. */
            hiob->residualLength = 0;      
            hiob->haStat = SCSI_HOST_DU_DO;
            SCSIhTerminateCommand(hiob);

            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID05);
            return;
         }

         if ((hiob->SCSI_IF_noUnderrun == 0) &&
             (hiob->haStat != SCSI_HOST_DETECTED_ERR))
         {
            /* Indicate overrun/underrun status */
            hiob->residualLength = 0;
            hiob->haStat = SCSI_HOST_DU_DO;
         }

         /* Restore back the mode */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

         return;
      }
#endif /* (SCSI_DOWNSHIFT_U320_MODE && !SCSI_ASPI_REVA_SUPPORT) */
#if (SCSI_PACKETIZED_IO_SUPPORT)
#if (!SCSI_DCH_U320_MODE && (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE))
      if (SCSI_hHARPOON_REV_A(hhcb))
      {
         /* The bit bucket feature does not work properly in Harpoon2 Rev A  */
         /* chip during packetized transfers.  The problem is that the chip  */
         /* does not know when to stop bit bucketting.  The chip does not    */
         /* stop even after a phase change.  We have no choice but to reset  */
         /* the SCSI bus and start over again.  The details of this hardware */
         /* problem can be found in the Razor database entry #2. */
         if (SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))
         {
            /* Terminate the I/O with an appropriate error. */
            hiob->residualLength = 0;      
            hiob->haStat = SCSI_HOST_DU_DO;
            SCSIhTerminateCommand(hiob);
   
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID63);
            return;
         }
      }
#endif /* (!SCSI_DCH_U320_MODE && (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)) */
#endif /* (SCSI_PACKETIZED_IO_SUPPORT) */

#if !SCSI_DCH_U320_MODE
      ubyteWork = OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL));
#else      
      /* Direction bit no longer resides in dfcntrl reg .. $$ DCH $$ */
      ubyteWork = OSD_INEXACT8(SCSI_AICREG(SCSI_DFSTATUS));
#endif /* !SCSI_DCH_U320_MODE */
      phase = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIPHASE));

      /* If the SCSI bus is still in the data phase... */
      if (((ubyteWork & SCSI_DIRECTION) && (phase == SCSI_DOUT)) ||
          (!(ubyteWork & SCSI_DIRECTION) && (phase == SCSI_DIN)))
      {
         /* ...bit bucket the overflown bytes until the target changes */
         /* SCSI phase. */

         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
         ubyteWork = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), ubyteWork | SCSI_BITBUCKET);
         SCSI_hSETMODEPTR(hhcb,scsiRegister,originalMode);

         i = 0;
         while ((((stat = OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1))) &
                   SCSI_PHASEMIS) == 0) &&
                (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
         {
            if ((stat & (SCSI_BUSFREE | SCSI_SCSIRSTI)))
            {
               break;
            }
            OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
         }

         if (i >= SCSI_REGISTER_TIMEOUT_COUNTER)
         {
            /* Terminate the I/O with an appropriate error. */
            hiob->residualLength = 0;      
            hiob->haStat = SCSI_HOST_DU_DO;
            SCSIhTerminateCommand(hiob);

            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID05);
            return;
         }

         /* Empty the Data FIFO (this can be done anytime after bit bucketing*/
         /* has been turned ON and before it is turned off).  This is because*/
         /* the FIFO may still have some bytes from the target due to the    */
         /* fact that REQ can still keep coming from the target even after   */
         /* the last SG count has exhausted and before bitbucketing could be */
         /* turned on. */
         if (!SCSIhClearDataFifo(hhcb,hiob,originalMode))
         {
            /* SCSIhBadSeq was called so just terminate */
            return;
         }

         /* Disable Bit bucketing */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), ubyteWork);
         SCSI_hSETMODEPTR(hhcb,scsiRegister,originalMode);
      }
      else
      {
         /* In Harpoon2, the auto-ACK feature will ACK the bytes in the case */
         /* of non-packetized DATA-IN even though the last SG count has      */
         /* exhausted.  This ACKing can cause the target to get out of the   */
         /* DATA-IN phase leaving the overflown bytes in the Data FIFO.      */
         /* Empty the Data FIFO to clear the overrun data.                   */
         if (!SCSIhClearDataFifo(hhcb,hiob,originalMode))
         {
            /* SCSIhBadSeq was called so just terminate */
            return;
         }
      }

      /* Reset the residue length to indicate that an overrun has occurred. */
      hiob->residualLength = 0;      
   }

   if ((hiob->SCSI_IF_noUnderrun == 0) &&
       (hiob->haStat != SCSI_HOST_DETECTED_ERR))
   {
       /* Indicate overrun/underrun status */
       hiob->haStat = SCSI_HOST_DU_DO;
   }

   /* Restore back the mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
}

/*********************************************************************
*
*  SCSIhStandardU320Underrun
*
*     Examine SCB fields to determine if an underrun has occurred.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*
*  Remarks:                
*                 Returns a value of 1 if SCB indicates underrun 
*                 has occurred.
*
*                 Assumes correct mode on entry.
*                  
*********************************************************************/
#if SCSI_STANDARD_U320_MODE
SCSI_UEXACT8 SCSIhStandardU320Underrun (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 underrun;

   /* If the SCB did not need any data bytes to be transferred in the first  */
   /* place or the sequencer has identified ahead of time that all the bytes */
   /* have been transferred (this could happen in a scenario where a Save    */
   /* Data Pointers request comes from the target exactly after all the S/G  */
   /* elements have been transferred [because of SDP message in non-         */
   /* packetized mode or a legal phase change in packetized mode], and the   */
   /* target reselects later and tries to send more data) then it is an      */
   /* overrun. (sg_cache_SCB will have the 'no_data' flag set in both these  */
   /* cases) */
   if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,sg_cache_SCB))) & SCSI_SU320_NODATA)
   {
      underrun = 0;
   }
   /* Else, if the sequencer has identified that all the bytes have been     */
   /* transferred (sg_cache_work will have the 'no_data' flag set in this    */
   /* case) then it is an overrun else it is an underrun. */
   else
   if ((OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_sg_cache_work))) &
         SCSI_SU320_NODATA))
   {
      underrun = 0;
   }
   else
   {
      underrun = 1;
   }

   /* To prevent the sequencer from reporting a spurious underrun after an   */
   /* overrun for the same SCB, set the 'no_data' flag in the sg_cache_SCB   */
   /* field of the SCB. */
   if (!underrun)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_U320,sg_cache_SCB)),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_U320,sg_cache_SCB))) |
         SCSI_SU320_NODATA));
   }

   return (underrun);
}
#endif /* SCSI_STANDARD_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardEnhU320Underrun
*
*     Examine SCB fields to determine if an underrun has occurred.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*
*  Remarks:                
*                 Returns a value of 1 if SCB indicates underrun 
*                 has occurred.
*
*                 Assumes correct mode on entry.
*                  
*********************************************************************/
#if SCSI_STANDARD_ENHANCED_U320_MODE
SCSI_UEXACT8 SCSIhStandardEnhU320Underrun (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 underrun;

   /* If the SCB did not need any data bytes to be transferred in the first  */
   /* place or the sequencer has identified ahead of time that all the bytes */
   /* have been transferred (this could happen in a scenario where a Save    */
   /* Data Pointers request comes from the target exactly after all the S/G  */
   /* elements have been transferred [because of SDP message in non-         */
   /* packetized mode or a legal phase change in packetized mode], and the   */
   /* target reselects later and tries to send more data) then it is an      */
   /* overrun. (sg_cache_SCB will have the 'no_data' flag set in both these  */
   /* cases) */
   if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,sg_cache_SCB))) & SCSI_SEU320_NODATA)
   {
      underrun = 0;
   }
   /* Else, if the sequencer has identified that all the bytes have been     */
   /* transferred (sg_cache_work will have the 'no_data' flag set in this    */
   /* case) then it is an overrun else it is an underrun. */
   else
   if ((OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_sg_cache_work))) &
         SCSI_SEU320_NODATA))
   {
      underrun = 0;
   }
   else
   {
      underrun = 1;
   }

   /* To prevent the sequencer from reporting a spurious underrun after an   */
   /* overrun for the same SCB, set the 'no_data' flag in the sg_cache_SCB   */
   /* field of the SCB. */
   if (!underrun)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,sg_cache_SCB)),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,sg_cache_SCB))) |
         SCSI_SEU320_NODATA));
   }

   return (underrun);
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/*********************************************************************
*
*  SCSIhDchU320Underrun
*
*     Examine SCB fields to determine if an underrun has occurred.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*
*  Remarks:                
*                 Returns a value of 1 if SCB indicates underrun 
*                 has occurred.
*
*                 Assumes correct mode on entry.
*                  
*********************************************************************/
#if SCSI_DCH_U320_MODE
SCSI_UEXACT8 SCSIhDchU320Underrun (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 underrun;

   /* If the SCB did not need any data bytes to be transferred in the first  */
   /* place or the sequencer has identified ahead of time that all the bytes */
   /* have been transferred (this could happen in a scenario where a Save    */
   /* Data Pointers request comes from the target exactly after all the S/G  */
   /* elements have been transferred [because of SDP message in non-         */
   /* packetized mode or a legal phase change in packetized mode], and the   */
   /* target reselects later and tries to send more data) then it is an      */
   /* overrun. (sg_cache_SCB will have the 'no_data' flag set in both these  */
   /* cases) */
   if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,sg_cache_SCB))) & SCSI_DCHU320_NODATA)
   {
      underrun = 0;
   }
   /* Else, if the sequencer has identified that all the bytes have been     */
   /* transferred (sg_cache_work will have the 'no_data' flag set in this    */
   /* case) then it is an overrun else it is an underrun. */
   else
   if ((OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DCH_U320,sg_cache_SCB))) &
         SCSI_DCHU320_NODATA))
   {
      underrun = 0;
   }
   else
   {
      underrun = 1;
   }

   /* To prevent the sequencer from reporting a spurious underrun after an   */
   /* overrun for the same SCB, set the 'no_data' flag in the sg_cache_SCB   */
   /* field of the SCB. */
   if (!underrun)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DCH_U320,sg_cache_SCB)),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DCH_U320,sg_cache_SCB))) |
         SCSI_DCHU320_NODATA));
   }

   return (underrun);
}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhDownshiftU320Underrun
*
*     Examine SCB fields to determine if an underrun has occurred.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*
*  Remarks:                
*                 Returns a value of 1 if SCB indicates underrun 
*                 has occurred.
*
*                 Assumes correct mode on entry.
*                  
*********************************************************************/
#if SCSI_DOWNSHIFT_U320_MODE
SCSI_UEXACT8 SCSIhDownshiftU320Underrun (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 underrun;
   
   /* If the SCB did not need any data bytes to be transferred in the first  */
   /* place or the sequencer has identified ahead of time that all the bytes */
   /* have been transferred (this could happen in a scenario where a Save    */
   /* Data Pointers request comes from the target exactly after all the S/G  */
   /* elements have been transferred [because of SDP message in non-         */
   /* packetized mode or a legal phase change in packetized mode], and the   */
   /* target reselects later and tries to send more data) then it is an      */
   /* overrun. (sg_cache_SCB will have the 'no_data' flag set in both these  */
   /* cases) */
   if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,sg_cache_SCB))) & SCSI_DU320_NODATA)
   {
      underrun = 0;
   }
   /* Else, if the sequencer has identified that all the bytes have been     */
   /* transferred (sg_cache_work will have the 'no_data' flag set in this    */
   /* case) then it is an overrun else it is an underrun. */
   if ((OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_sg_cache_work))) &
         SCSI_DU320_NODATA))
   {
      underrun = 0;
   }
   else
   {
      underrun = 1;
   }

   /* To prevent the sequencer from reporting a spurious underrun after an   */
   /* overrun for the same SCB, set the 'no_data' flag in the sg_cache_SCB   */
   /* field of the SCB. */
   if (!underrun)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,sg_cache_SCB)),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,sg_cache_SCB))) |
         SCSI_DU320_NODATA));
   }

   return (underrun);
}
#endif /* SCSI_DOWNSHIFT_U320_MODE */


/*********************************************************************
*
*  SCSIhDownshiftEnhU320Underrun
*
*     Examine SCB fields to determine if an underrun has occurred.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*
*  Remarks:                
*                 Returns a value of 1 if SCB indicates underrun 
*                 has occurred.
*
*                 Assumes correct mode on entry.
*                  
*********************************************************************/
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
SCSI_UEXACT8 SCSIhDownshiftEnhU320Underrun (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 underrun;
   
   /* If the SCB did not need any data bytes to be transferred in the first  */
   /* place or the sequencer has identified ahead of time that all the bytes */
   /* have been transferred (this could happen in a scenario where a Save    */
   /* Data Pointers request comes from the target exactly after all the S/G  */
   /* elements have been transferred [because of SDP message in non-         */
   /* packetized mode or a legal phase change in packetized mode], and the   */
   /* target reselects later and tries to send more data) then it is an      */
   /* overrun. (sg_cache_SCB will have the 'no_data' flag set in both these  */
   /* cases) */
   if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,sg_cache_SCB))) & SCSI_DEU320_NODATA)
   {
      underrun = 0;
   }
   /* Else, if the sequencer has identified that all the bytes have been     */
   /* transferred (sg_cache_work will have the 'no_data' flag set in this    */
   /* case) then it is an overrun else it is an underrun. */
   if ((OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_sg_cache_work))) &
         SCSI_DEU320_NODATA))
   {
      underrun = 0;
   }
   else
   {
      underrun = 1;
   }

   /* To prevent the sequencer from reporting a spurious underrun after an   */
   /* overrun for the same SCB, set the 'no_data' flag in the sg_cache_SCB   */
   /* field of the SCB. */
   if (!underrun)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,sg_cache_SCB)),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,sg_cache_SCB))) |
         SCSI_DEU320_NODATA));
   }

   return (underrun);
}
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */

#if SCSI_DOMAIN_VALIDATION
/*********************************************************************
*
*  SCSIhDVTimeout
*                                             
*     Handle DV timeout, attempt bit bucketting to force data phase 
*     change.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Incoming Mode : Don't Care         
*                  
*********************************************************************/
void SCSIhDVTimeout (SCSI_HHCB SCSI_HPTR hhcb,
                     SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UINT32 i = 0;          /* time out counter */
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 modeSave;
   SCSI_UEXACT8 stat;
   SCSI_UEXACT8 ubyteWork;
   SCSI_UEXACT16 scbNumber; 
   
   /* Save the incoming mode, and put in the mode that we got the interrupt, */
   /* which should be either 0, or 1 or even 3 if in packetized mode*/
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);
   
   /* Save SCBPTR register. */ 
   scbNumber = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
      ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);
      
   if (!(SCSI_IS_NULL_SCB(scbNumber)))
   {
      hiob = SCSI_ACTPTR[scbNumber];
   }
   
   if (hiob == SCSI_NULL_HIOB)
   {
      /*If selection in progress with DV timeout then use scsi_waiting_scb.  */
      /*If selingo with data phase (data phase required to get dv timeout)   */
      /*then packetized LQO hangup occured.                                  */
      /*Legacy SCSI should not enter this function without a valid hiob.     */
      /*If NULL HIOB then abort target.                                      */
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & SCSI_SELINGO) 
      {
         scbNumber = ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
                     (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));
      }
      else
      {
         SCSI_SET_NULL_SCB(scbNumber);
      }
      
      if (SCSI_IS_NULL_SCB(scbNumber))
      {
         hiob = SCSI_NULL_HIOB;
      }
      else
      {
         hiob = SCSI_ACTPTR[scbNumber];
      }
      
      if (hiob == SCSI_NULL_HIOB)
      {
         SCSIhAbortTarget(hhcb,hiob);
         return;
      }      
   }
   
#if SCSI_PACKETIZED_IO_SUPPORT
#if (!SCSI_DCH_U320_MODE && (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE))
   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      /* The bit bucket feature does not work properly in Harpoon2 Rev A  */
      /* chip during packetized transfers.  The problem is that the chip  */
      /* does not know when to stop bit bucketting.  The chip does not    */
      /* stop even after a phase change.  We have no choice but to reset  */
      /* the SCSI bus and start over again.  The details of this hardware */
      /* problem can be found in the Razor database entry #2. */
      if (SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))
      {
         /* Terminate the I/O with an appropriate error. */
         hiob->residualLength = 0;      
         hiob->haStat = SCSI_HOST_DU_DO;
         SCSIhTerminateCommand(hiob);
   
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID60);
         return;
      }
   }
#endif /* (!SCSI_DCH_U320_MODE && (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)) */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

   SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);
   
#if !SCSI_DCH_U320_MODE
   ubyteWork = OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL));
#else
   /* The direction bit was moved to dfstatus reg .. $$ DCH $$ */
   ubyteWork = OSD_INEXACT8(SCSI_AICREG(SCSI_DFSTATUS));

#endif /* !SCSI_DCH_U320_MODE */
   /* Warning: the SCSISIGI register must be used to determine
    * if the bus is in data phase as the SCSIPHASE register returns
    * a value of 0 if there are no REQs outstanding.
    */
   /*The SCSISIGI reg may also be invalid while connected in IU mode   */ 
   /* where not all the bits on the bus can be read.  If Rev B do check*/ 
   /*  for packetized and attempt bit bucket if B.                     */
   phase = (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_BUSPHASE);
   /* If the SCSI bus is still in the data phase... */
   if (((phase & SCSI_CDO) == 0) &&
       (((ubyteWork & SCSI_DIRECTION) && ((phase & SCSI_IOO) == 0)) ||
          ((!(ubyteWork & SCSI_DIRECTION)) && (phase & SCSI_IOO)) ||
             ((SCSI_hHARPOON_REV_B(hhcb) || SCSI_hDCH_SCSI_CORE(hhcb)) &&
                (OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT2)) & SCSI_LQPACKETIZED))))
   {
      /* ...bit bucket the overflown bytes until the target changes the  */
      /* the SCSI phase. */
      /* Since Harpoon2 Rev B chip supports bit-bucketting in both the      */
      /* packetized and non-packetized mode, we need to handle accordingly. */

      if (SCSI_hHARPOON_REV_B(hhcb) || SCSI_hDCH_SCSI_CORE(hhcb))
      {
         /* Turn on the bit bucketting. */
         ubyteWork = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL),
            ubyteWork | SCSI_M01BITBUCKET);
         
         /* The hardware automatically turns it off when the bit bucketting */
         /* is completed. Simply wait for the event. */
         i = 0;
         while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL)) &
                   SCSI_M01BITBUCKET) && (i++ < SCSI_DV_REGISTER_TIMEOUT_COUNTER))
         {
            stat = OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1));
            if ((stat & (SCSI_BUSFREE | SCSI_SCSIRSTI)))
            {
               break;
            }
            OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
         }

         if (i > SCSI_DV_REGISTER_TIMEOUT_COUNTER)
         {
            /* Terminate the I/O with an appropriate error. */
            hiob->residualLength = 0;      
            hiob->haStat = SCSI_HOST_DU_DO;
            SCSIhTerminateCommand(hiob);

            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID08);
            return;
         }
      }
      else
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
         ubyteWork = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), ubyteWork | SCSI_BITBUCKET);
         SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);

         while ((((stat = OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1))) &
                   SCSI_PHASEMIS) == 0) &&
                (i++ < SCSI_DV_REGISTER_TIMEOUT_COUNTER))
         {
            if ((stat & (SCSI_BUSFREE | SCSI_SCSIRSTI)))
            {
               break;
            }
            OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
         }

         if (i > SCSI_DV_REGISTER_TIMEOUT_COUNTER)
         {
            /* Terminate the I/O with an appropriate error. */
            hiob->residualLength = 0;      
            hiob->haStat = SCSI_HOST_DU_DO;
            SCSIhTerminateCommand(hiob);

            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID08);
            return;
         }
         /* Disable Bit bucketing */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), ubyteWork);
      }
         
      /* scb should contain zero residual value */
      hiob->residualLength = 0;      
               
      /* Empty the Data FIFO (this can be done anytime after bit bucketing*/
      /* has been turned ON and before it is turned off).  This is because*/
      /* the FIFO may still have some bytes from the target due to the    */
      /* fact that REQ can still keep coming from the target even after   */
      /* the last SG count has exhausted and before bitbucketing could be */
      /* turned on. */

      SCSIhResetDataChannel(hhcb,hhcb->SCSI_HP_originalMode);
   }
   else 
   {
      /* If packetized but data phase not detected */
      if ((SCSI_hHARPOON_REV_B(hhcb) || SCSI_hDCH_SCSI_CORE(hhcb)) &&
         (OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT2)) & SCSI_LQPACKETIZED) )
      {
         /* Terminate the I/O with an appropriate error. */
         hiob->residualLength = 0;      
         hiob->haStat = SCSI_HOST_DU_DO;
         SCSIhTerminateCommand(hiob);
   
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID60);
         return;
      }
   }
      
      
   /* Indicate overrun/underrun status */
   hiob->haStat = SCSI_HOST_DU_DO;
 
   /* Restore back the mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

}
#endif /*SCSI_DOMAIN_VALIDATION*/


/*********************************************************************
*
*  SCSIhCdbAbort
*
*     An unexpected phase change occurred during the CDB transfer
*     in Command phase. 
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 
*  Remarks:       The sequencer generates this interrupt when
*                 the target changes phase while CDB bytes are
*                 still to be transferred and the new phase is not
*                 Status.
*
*                 The handling is as follows:
*                 0) Set the MODE_PTR to original mode.
*                 1) If the new phase is not message-in then 
*                    the phase is invalid, full error recovery
*                    is performed (i.e. SCSIhBadSeq call), and
*                    the sequencer is restarted at the idle loop.
*                    Note that; a target may switch to data phase
*                    rather than attempt to disconnect after receiving
*                    a CDB if the initiator specified a CDB length
*                    greater than what the target expected. Therefore,
*                    we may want to consider improving the handling
*                    as currently SCSIhBadSeq is called in this case.
*                    Improved handling might be asserting ATN and 
*                    accepting and discarding data (if Data-in phase).
*                    If the phase was Data-out then we'd probably still
*                    call SCSIhBadSeq as we'd want to avoid data
*                    corruption.   
*                 Otherwise:
*                 2) The data channel is cleared in case the CDB
*                    was transferred via the DMA engine.
*                 3) If the new phase is message-in then check if
*                    the incoming message is a Restore Pointers as
*                    the target may have detected a parity error
*                    while transferring the CDB, the message is 
*                    ACKed, and the sequencer is restarted at
*                    the SCSI_hSIOSTR3_ENTRY point.  
*                 4) If the incoming message is not Restore Pointers
*                    then assert ATN in an attempt to get the target
*                    to switch to message out phase.
*                 5) If the target does not switch to message
*                    out phase full error recovery is performed,
*                    (i.e. call SCSIhBadSeq which results in a
*                    SCSI bus reset, HA_FAILED OSMEvent), and
*                    the sequencer is restarted at the idle loop.
*                 6) If the target does switch to message out phase,
*                    the HIM sends either an Abort Task (for a tagged
*                    request) or an Abort Task Set(untagged request).
*                    The target should then switch to bus free phase.
*                    If the target does not switch to bus free, then
*                    full error recovery is performed by calling
*                    SCSIhBadSeq.
*                 7) Lastly, if SCSIhBadSeq was not called, the HIOB
*                    is completed with a SCSI_HOST_DETECTED_ERR
*                    and the sequencer is restarted at the idle loop.
*
*                 On exit the mode (SCSI_MODE_PTR register) is set to
*                 the original mode when the interrupt occurred unless
*                 SCSIhBadSeq was called. 
*                  
*********************************************************************/
#if (!SCSI_BIOS_SUPPORT && !SCSI_BEF_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1)
void SCSIhCdbAbort (SCSI_HHCB SCSI_HPTR hhcb,
                    SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 scsidatl;
   SCSI_UINT32 i = 0;          /* time out counter */

   /* Set the mode to the original mode at the time of the interrupt.  
    * Should be one of the data channel modes (0, or 1).
    */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);

   if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_BUSPHASE) != SCSI_MIPHASE)
   {
      /* If the phase is not message-in assume the phase is invalid
       * and perform full error recovery.
       */
      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID06);
      return;
   }

   /* Note; sequencer has already set DFCNTRL = 0, but do it
    * again. However, the sequencer does not wait for HDMAENACK!
    * Therefore, the data channel should be disabled, in case
    * the CDB was DMAed (currently when CDB is greater than 12 bytes).
    */
   if (SCSIhDisableDataChannel(hhcb,hhcb->SCSI_HP_originalMode))
   {
      /* Something wrong channel never disabled. */
      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID35);
      return;
   }

   /* Reset DMA & SCSI transfer logic */
   SCSIhResetDataChannel(hhcb,hhcb->SCSI_HP_originalMode);

   if (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIBUSL)) == SCSI_MSG03)
   {
      /* If the message from the target is a Restore Pointers
       * then the target may have detected a parity error during
       * the CDB transfer.
       * We've already cleared the data channel so simply ACK the 
       * message and restart the sequencer at SIOSTR3_ENTRY in the
       * mode that sequencer was in at the time of the interrupt
       * (SCSI_HP_originalMode).
       */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE);

      scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
          (SCSI_UEXACT8)(SCSI_hSIOSTR3_ENTRY(hhcb) >> 2));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
          (SCSI_UEXACT8)(SCSI_hSIOSTR3_ENTRY(hhcb) >> 10));

      /* When sequencer detects SCSI phase changed from Command phase to  */
      /* other SCSI phases than Status phase, it interrupts CHIM with the */
      /* intcode 'CDB_XFER_PROBLEM' and the mode_ptr is either 0 or 1.    */
      /* This mode becomes CHIM's original mode during interrupt context. */
      /* At the end of the 'CDB_XFER_PROBLEM' interrupt handling, CHIM    */
      /* starts the sequencer at 'siostr3_entry'.  The sequencer will jump*/
      /* to the 'siostr3'.  In this problem, the target goes to command   */
      /* phase after CHIM acked Restore Data Pointer message byte.        */
      /* Therefore, sequencer will go to label 'sio150'.  As the comment  */
      /* in the sequencer.asm file, the mode_ptr should be in mode 3 when */ 
      /* entering the label 'sio150'.  However,  CHIM did not set to mode */
      /* 3 because CHIM always restore the original mode (in this case,   */
      /* the mode is either 0 or 1) at the end of the interrupt handling. */
      /* This causes the sequencer to not enter the right path to retry   */
      /* the cdb transfer. */
      /* Solution: set the original mode to 3 at the end of               */
      /* CDB_XFER_PROBLEM interrupt handling routine.  This fix requires  */
      /* an update to the Harpoon HIM/Sequencer interface spec.           */
      hhcb->SCSI_HP_originalMode = 0x33;
   }
   else
   {
      /* Some other message received. Attempt to get the target to
       * switch to message out phase and issue an ABORT TASK or ABORT
       * TASK SET message.
       */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE | SCSI_ATNO);
      do
      {
         scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
         phase = SCSIhWait4Req(hhcb);
      } while (phase == SCSI_MIPHASE);

      if (phase != SCSI_MOPHASE)
      {
         /* If the target does not switch to message out phase
          * then perform full error recovery.
          */
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID07);
         return;
      }

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MOPHASE);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);

      /* Abort the I/O by sending the relevant ABORT TASK message. */

      /* Disable the 'Unexpected bus free' interrupt, as we are going to
       * expect one, after sending the abort message.
       */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE1),
                    OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE1)) & ~SCSI_ENBUSFREE);

      /* Send the appropriate Abort message to the target. */
#if (SCSI_TRIDENT_WORKAROUND && SCSI_TARGET_OPERATION)
      /* Do not remove this code until we verify that it's not
       * required for U320 ASIC.
       */
      /* Note; the SPIOEN target mode bug is supposed to be fixed in Harpoon
       * ASIC. However, keep this code just in case.
       * If SPIOEN is turned on when we go bus free, the trident chip would
       * assert a REQ during the following selection process when it acts as
       * a target.  This is a hardware problem.  To workaround this problem
       * we need to manually ACK the last message (ABORT TASK in this case) 
       * by turning off SPIOEN.
       */

      /* Requires Mode 3 */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
      
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL0),
                    (OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL0)) & ~SCSI_SPIOEN ));

      /* Restore original mode. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);

      /* Setup SCSIDATL with the ABORT TASK message. Note that ACK will
       * not go out, because SPIOEN has been turned off.
       */
      if ((hiob->SCSI_IF_tagEnable) &&
          (!hiob->SCSI_IF_disallowDisconnect))
      {
         /* Abort tagged command */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL),SCSI_MSG0D);
      }
      else
      {
         /* Abort non-tagged command */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL),SCSI_MSG06);
      }

      /* Manual ack of abort msg */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO),SCSI_ACKO);

      /* Wait for REQ to go down */
      i = 0;
      while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_REQI) &&
             (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
      {
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }

      /* Release the bus */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO),0);

#else /* (SCSI_TRIDENT_WORKAROUND && SCSI_TARGET_OPERATION) */
      if ((hiob->SCSI_IF_tagEnable) &&
          (!hiob->SCSI_IF_disallowDisconnect))
      {
         /* Abort tagged command */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL),SCSI_MSG0D);
      }
      else
      {
         /* Abort non-tagged command */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL),SCSI_MSG06);
      }

#endif /* (SCSI_TRIDENT_WORKAROUND && SCSI_TARGET_OPERATION) */

      /* Wait for the bus to go to BUS FREE phase. */
      i = 0;
      while (i++ < SCSI_REGISTER_TIMEOUT_COUNTER)
      {
         if (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) &
             (SCSI_BUSFREE | SCSI_SCSIRSTI))
         {
            /* Bus has gone to BUS FREE phase or a SCSI bus reset
             * occurred.
             */
            break;
         }

         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }

      if (i >= SCSI_REGISTER_TIMEOUT_COUNTER)
      {
         /* Bus did not go to BUS FREE phase. 
          * Consider this as a bad SCSI sequence.
          */
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID36);
         return;
      }

      /* Terminate the I/O with an appropriate error. */
      hiob->haStat = SCSI_HOST_DETECTED_ERR;
      
#if SCSI_CRC_NOTIFICATION
      hiob->residualLength = SCSI_PARITY_CDB_ERR;
#endif /* SCSI_CRC_NOTIFICATION */

      /* Clear Busy Target Table entry. */
      SCSI_hTARGETCLEARBUSY(hhcb,hiob);

      SCSIhTerminateCommand(hiob);

      /* Put the sequencer back in the idle loop */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
          (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
          (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));
   }
}
#endif /* (!SCSI_BIOS_SUPPORT && !SCSI_BEF_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1) */

/*********************************************************************
*
*  SCSIhCheckCondition
*
*     Handle response to target check condition
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       The sequencer expects the HWM to do the following:
*                 a) Disable bus free interrupt - sequencer will
*                    re-enable in the idle loop 
*                 b) ACK the Command Complete message
*                 c) Clear the Busy Target entry for the active
*                    target/Lun
*                 d) Disable the data channel if a data in transfer
*                    was active
*                 e) Restart the sequencer at the idle loop.    
*
*                 SCSI_MODE_PTR may have changed.   
*********************************************************************/
void SCSIhCheckCondition (SCSI_HHCB SCSI_HPTR hhcb,
                          SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_UEXACT8 status;
#if !(SCSI_STANDARD_U320_MODE && SCSI_TARGET_OPERATION)
   SCSI_UEXACT8 scsidatl;
#endif /* !(SCSI_STANDARD_U320_MODE && SCSI_TARGET_OPERATION) */
   SCSI_UINT32 i;       /* time out counter */
#if SCSI_BIOS_SUPPORT
   SCSI_UEXACT8 bios_originalmode;  

   SCSIHReadScratchRam(hhcb,
                       (SCSI_UEXACT8 SCSI_SPTR) &bios_originalmode,
                       (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ORIGINALMODE),
                       (SCSI_UEXACT16) 1);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,bios_originalmode);

#else /* !SCSI_BIOS_SUPPORT */
   /* Set the mode in the incomming mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);
#endif /* SCSI_BIOS_SUPPORT */

   if (hiob->SCSI_IP_mgrStat == SCSI_MGR_AUTOSENSE)
   {
      hiob->haStat = SCSI_HOST_SNS_FAIL;
      status = 0;
   }
   else
   {
      status = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_hPASS_TO_DRIVER(hhcb)));
      hiob->trgStatus = status;         /* record UNIT CHECK condition */
#if SCSI_DATA_IN_RETRY_DETECTION
      if (status == SCSI_UNIT_GOOD)
      {
         /* Special case of target successfully performed a read  */
         /* retry operation. For certain RAID XOR implementations */
         /* we need to fail the I/O as the XOR may already have   */
         /* been performed on the data.                           */
         hiob->haStat = SCSI_HOST_READ_RETRY;
      }   
#endif /* SCSI_DATA_IN_RETRY_DETECTION */
   }

#if !SCSI_DOWNSHIFT_MODE
   if (((hiob->SCSI_IF_freezeOnError) ||
       (status == SCSI_UNIT_BUSY) ||
       (status == SCSI_UNIT_QUEFULL)) &&
       !(SCSI_TARGET_UNIT(hiob)->targetControl->freezeMap &
         SCSI_FREEZE_HIPRIEXISTS))
   {
      SCSI_hFREEZEHWQUEUE(hhcb,hiob,SCSI_AE_FREEZEONERROR_START);
   }
#endif /* !SCSI_DOWNSHIFT_MODE */
   
   /* Clear target busy entry */ 
   SCSI_hTARGETCLEARBUSY(hhcb,hiob);


   /* Disable the data channel if inbound data transfer I/O (SCSI->PCI)
    * Note; sequencer disables channel in data-out direction.
    * Also, the following wait must be done prior to posting 
    * the completion for interfacing to an OSM programming
    * to the HWM layer. 
    * This code is equivalent to sequencer label siosta8.
   */ 
#if SCSI_BIOS_SUPPORT
   if ((bios_originalmode == SCSI_MODE0) ||
       (bios_originalmode == SCSI_MODE1))
   {
      /* Restore mode to original mode at time of interrupt. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,bios_originalmode);
#else   
   if ((hhcb->SCSI_HP_originalMode == SCSI_MODE0) ||
       (hhcb->SCSI_HP_originalMode == SCSI_MODE1))
   {
      /* Restore mode to original mode at time of interrupt. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);
#endif /* !SCSI_BIOS_SUPPORT */

      /* If the DMA is still enabled, the data direction was
       * 'read', and the FIFO is still emptying to the host.
       * Wait for FIFO empty, and then disable the DMA. 
       */

      if (OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL)) & SCSI_HDMAENACK)
      {
         /* DMA is still enabled. The data direction was a 'read'
          * and the FIFO is still emptying to the host. 
          * Wait for FIFO empty, then disable DMA.
          */
         i = 0;
         while (!((OSD_INEXACT8(SCSI_AICREG(SCSI_DFSTATUS)) & (SCSI_FIFOEMP))) &&
                (i++ < SCSI_REGISTER_TIMEOUT_COUNTER)) 
         {
            /* Harpoon 2 Rev A4 set FIFOEMP bit when there is a wide residue */
            /* byte in the FIFO.  However, the logic has been removed in     */
            /* Harpoon 2 Rev B (don't know why?).  This causes CHIM to       */
            /* timeout and call BadSeq.  The current solution is CHIM will   */
            /* check for HDONE bit set in Harpoon 2 Rev B and break out the  */
            /* loop.  The bit set indicates all valid data have been emptied */
            /* out the data fifo. */
#if !SCSI_ASPI_REVA_SUPPORT
            if (SCSI_hHARPOON_REV_B(hhcb) || SCSI_hDCH_SCSI_CORE(hhcb))
            {
               if (OSD_INEXACT8(SCSI_AICREG(SCSI_DFSTATUS)) & SCSI_HDONE)
               {
                  break;
               }
            }
#endif /* !SCSI_ASPI_REVA_SUPPORT */
            OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
         }

         if (i >= SCSI_REGISTER_TIMEOUT_COUNTER)
         { 
            /* Something wrong hardware never cleared. */ 
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID31);
            return;
         }

         /* Disable the channel now */
#if SCSI_BIOS_SUPPORT
         if (SCSIhDisableDataChannel(hhcb,bios_originalmode))
#else
         if (SCSIhDisableDataChannel(hhcb,hhcb->SCSI_HP_originalMode))
#endif /* SCSI_BIOS_SUPPORT */
         {
            /* Something wrong channel never disabled. */
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID32);
            return;
         }
      }
   }

   if ((status == SCSI_UNIT_CHECK) && hiob->SCSI_IF_autoSense)
   {
      hiob->SCSI_IP_negoState = 0;
      hiob->snsResidual = 0;
      SCSI_hREQUESTSENSE(hhcb,hiob);   /* issue REQUEST SENSE command */
   }
   else
   {
      /* If there is a queue full, it will be handled in RSM layer
       * during posting.
       */
      SCSIhTerminateCommand(hiob);
   }

   /* Reset synchronous/wide negotiation only for CHECK CONDITION */
   /* reset sync/wide as long as configured to do so   */
   /* even if it's negotiated without sync/wide        */
   if (status == SCSI_UNIT_CHECK)
   {
      deviceTable = targetUnit->deviceTable;

#if SCSI_NEGOTIATION_PER_IOB
#if SCSI_DOMAIN_VALIDATION
      /* We should get the request sense data; so, we should make sure */
      /* we negotiate async/narrow during domain validation */
      if (hiob->SCSI_IF_dvIOB)
      {
         if (!deviceTable->SCSI_DF_suppressNego)
         {
            if SCSI_hWIDEORSYNCNEGONEEDED(deviceTable)
            {
               /* Make TEMP entry to be current negoXferIndex */
               deviceTable->negoXferIndex = SCSI_TEMP_ENTRY;
               SCSI_TEMP_XFER(deviceTable)[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
               SCSI_TEMP_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;

               SCSIhUpdateNeedNego(hhcb,targetUnit->scsiID);
            }
         }
      }
      else
#endif /* SCSI_DOMAIN_VALIDATION */
      /* If negotiation is forced, only then initiate both sync and wide */
      if (hiob->SCSI_IF_forceReqSenseNego)
      {
         SCSIhChangeNegotiation(targetUnit);
      }
#else /* SCSI_NEGOTIATION_PER_IOB */
#if !SCSI_BIOS_SUPPORT
      if (!deviceTable->SCSI_DF_suppressNego)
      {
         if SCSI_hWIDEORSYNCNEGONEEDED(deviceTable)
         {
#if SCSI_DOMAIN_VALIDATION
            /* We should get the request sense data; so, we should make sure */
            /* we negotiate async/narrow during domain validation */
            if (hiob->SCSI_IF_dvIOB)
            {
               /* Make TEMP entry to be current negoXferIndex */
               deviceTable->negoXferIndex = SCSI_TEMP_ENTRY;
               SCSI_TEMP_XFER(deviceTable)[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
               SCSI_TEMP_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;
            }
#endif /* SCSI_DOMAIN_VALIDATION */
            SCSIhUpdateNeedNego(hhcb,targetUnit->scsiID);
         }
      }

#endif /* !SCSI_BIOS_SUPPORT */

#endif /* SCSI_NEGOTIATION_PER_IOB */
   }
   
   /* Note: all code from here down could be performed in the
    *       sequencer - rather than holding up the host in the 
    *       interrupt handler.
    */
#if SCSI_BIOS_SUPPORT
   /* Restore mode to original mode at time of interrupt. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,bios_originalmode);

   if ((bios_originalmode == SCSI_MODE0) ||
       (bios_originalmode == SCSI_MODE1))
#else   
      /* Restore mode to original mode at time of interrupt. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);

   if ((hhcb->SCSI_HP_originalMode == SCSI_MODE0) ||
       (hhcb->SCSI_HP_originalMode == SCSI_MODE1))
#endif /* !SCSI_BIOS_SUPPORT */
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL), 
          (OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL)) | SCSI_CLRCHN)); 

      /* Clear the RET_ADDR and ACTIVE_SCB sequencer variables to indicate  */
      /* that the SCB is not active anymore for this mode. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_hRET_ADDR(hhcb)+1), (SCSI_UEXACT8)0xFF);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb)+1),
         (SCSI_UEXACT8)SCSI_NULL_SCB);

      /* Since the mode becomes free after we do clear channel, the hardware */
      /* can allocate the mode for a packetzied connection after the bus goes*/
      /* BUS-FREE (after we ACK the Command Complete message).  This means   */
      /* that a CONFIG4DATA interrupt could be pending for the sequencer when*/
      /* we unpause.  If we unpause the sequencer at the IDLE_LOOP_ENTRY this*/
      /* is what would happen:                                               */
      /*    - the sequencer would jump into its ISR to handle the CONFIG4DATA*/
      /*      interrupt where it enables the DMA and assigns the RET_ADDR to */
      /*      the relevant routine to return back to.                        */
      /*    - the sequencer then executes the first instruction at the       */
      /*      IDLE_LOOP_ENTRY which is to invalidate the RET_ADDR, which is  */
      /*      wrong.                                                         */
      /* We need to put the sequencer at the IDLE_LOOP_TOP entry to avoid the*/
      /* above problem.  Note that this entry point is defined for only the  */
      /* modes 0 and 1.  For other modes we still need to put the sequencer  */
      /* at IDLE_LOOP_ENTRY point. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                    (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_TOP(hhcb) >> 2));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                    (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_TOP(hhcb) >> 10));
   }
   else
   {
      /* Simply put the sequencer at the IDLE_LOOP_ENTRY point. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                    (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                    (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));
   }

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE1),
       (OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE1)) & ~SCSI_ENBUSFREE));

   /* The hardware is supposed to turn off SELDO when the 
    * SCSI bus goes free. However, we've seen cases 
    * where the sequencer gets in before the hardware
    * turns it off - apparently. Therefore turn SELDO
    * off manually prior to ACKing the Command 
    * Complete message.
    */   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT0), SCSI_CLRSELDO);  

#if (SCSI_STANDARD_U320_MODE && SCSI_TARGET_OPERATION)
   /* The target mode sequencer requires that SPIOEN
    * be disabled when entering the idle loop.
    * So perform a manual ACK of the Command Complete 
    * message for this sequencer.
    */
   /* Note: this code may not be required for U320. The
    *       SPIOEN problem is supposed to be fixed - allowing
    *       SPIOEN to be enabled for target mode without any
    *       negative side-effects.
    */
   /* Set mode 3 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL0),
                 (OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL0)) &
                     ~SCSI_SPIOEN));

   /* manual ack of command complete msg */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO),SCSI_ACKO);

   /* Wait for REQ to go down */
   i = 0;
   while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_REQI) &&
          (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
   {
      OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
   }

   /* Release the bus */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO),0);
#else
   /* ACK Command Complete message */
   scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
#endif /* (SCSI_STANDARD_U320_MODE && SCSI_TARGET_OPERATION) */
 
   /* Need to check for 'bus free status' to be set, before proceeding
    * to the sequencer.
    * Also, need to check for third party resets.
    */
   i = 0;
   while ((!(OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & 
             (SCSI_BUSFREE | SCSI_SCSIRSTI))) &&
          (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
   {
      OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
   }
}

/*********************************************************************
*
*  SCSIhPacketizedCheckCondition
*
*     Handle response to target check condition
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       None
*                  
*********************************************************************/
#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT)
void SCSIhPacketizedCheckCondition (SCSI_HHCB SCSI_HPTR hhcb,
                                    SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;
   SCSI_UEXACT8 status;
   SCSI_UEXACT16 iuScbNumber;
   SCSI_UEXACT8  i;
   SCSI_HIOB SCSI_IPTR iuhiob;

   status = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_hPASS_TO_DRIVER(hhcb)));

   /* The status can never be any value other than zero.  So, the following */
   /* check is just for sanity. */
   if (status != (SCSI_UEXACT8)0)
   {
      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID66);
      return;
   }

#if !SCSI_DOWNSHIFT_MODE
   if ((hiob != SCSI_NULL_HIOB) &&
       !(SCSI_TARGET_UNIT(hiob)->targetControl->freezeMap &
         SCSI_FREEZE_HIPRIEXISTS))
   {
      SCSI_hFREEZEHWQUEUE(hhcb,hiob,SCSI_AE_FREEZEONERROR_START);
   }
#endif /* !SCSI_DOWNSHIFT_MODE */
   
   /* Put the chip in the interrupted mode. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);

   /* Initialize the status buffer pointer in the HADDR preload register     */
   /* for sequencer use to DMA the status IU.  Note that we don't have to    */
   /* load the count in the HCNT register.  The sequencer will automatically */
   /* transfer all the bytes of the status IU into the status buffer.        */
   /* There are two assumptions here:                                        */
   /*   1. The size of the status buffer must be greater than or equal to the*/
   /*      maximum size of the Status IU, and                                */
   /*   2. The maximum size supported by the Harpoon chip is 2 KiloBytes (the*/
   /*      size of the Data FIFO). */

   /* Get bus address of the status buffer pointer */

#if SCSI_ASPI_SUPPORT
   OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,SCSI_SPI_STATUS_PTR(hhcb));
   busAddress = hhcb->busAddress;
#else
   busAddress =
      OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,SCSI_SPI_STATUS_PTR(hhcb));
#endif /* SCSI_ASPI_SUPPORT */      
   i = hhcb->SCSI_HP_originalMode & 01;
   if (i)
   {
      /* Status IU buffer can hold 2 IU status, one from each fifo. */
      /* Put fifo 1 data into 2nd half of spi status buffer.        */
      OSD_ADJUST_BUS_ADDRESS(hhcb,&busAddress,SCSI_MAX_STATUS_IU_SIZE);
   }
  
   /* Check for free status buffer if NULL SCB */
   if (!SCSI_IS_NULL_SCB(hhcb->SCSI_SPI_STATUS_IU_SCB[i]))
   {
      /* This buffer still has a previous SCB's request sense data. */
      /* The fifo would not be free if previous data not xfer'd from fifo. */
      iuScbNumber = hhcb->SCSI_SPI_STATUS_IU_SCB[i]; 
      iuhiob = SCSI_ACTPTR[iuScbNumber];
      
      /* If valid hiob get sense data for previous SCB */
      if (iuhiob != SCSI_NULL_HIOB)
      {
         SCSIhPacketizedGetSenseData(iuhiob);
      }
   }
   /* Reserve this buffer for this hiob SCB */
   hhcb->SCSI_SPI_STATUS_IU_SCB[i] = hiob->scbNumber;

#if (OSD_BUS_ADDRESS_SIZE == 64)
   /* load bus address to scratch ram */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR0),
                      ((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR1),
                      ((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order1);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR2),
                      ((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order2);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR3),
                      ((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order3);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR4),
                      ((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order4);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR5),
                      ((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order5);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR6),
                      ((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order6);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR7),
                      ((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order7);
#else /* (OSD_BUS_ADDRESS_SIZE != 64) */
   /* load bus address to scratch ram */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR0),
                      ((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR1),
                      ((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order1);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR2),
                      ((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order2);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR3),
                      ((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order3);
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */

   /* Remember the fact that Check Condition has happened for the HIOB. */
   hiob->trgStatus = SCSI_UNIT_CHECK;
}
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT) */

/******************************************************************************
*
*  SCSIhPacketizedGetSenseData
*
*     Move the Sense Data from the internal buffer to the appropriate
*     destination, which could be either 'snsBuffer' of HIOB or
*     'senseBuffer' of SCSI_UNIT_CONTROL.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       This routine is called after a command for a packetized I/O
*                 completes with a bad status packet.
*                  
******************************************************************************/
#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT)
void SCSIhPacketizedGetSenseData (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_HHCB SCSI_HPTR hhcb = SCSI_TARGET_UNIT(hiob)->hhcb;
#if SCSI_NEGOTIATION_PER_IOB
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);
#endif /* SCSI_NEGOTIATION_PER_IOB */                     
   SCSI_UEXACT8 SCSI_HPTR statusPtr = SCSI_SPI_STATUS_PTR(hhcb);   
   SCSI_UEXACT8 status, flag;
   SCSI_UEXACT8 pkStatus, pkLength;
   SCSI_UEXACT8 senseLen;
   SCSI_UEXACT8 packetizeErrorCode;
   SCSI_UEXACT8 i;
#if (SCSI_TASK_SWITCH_SUPPORT != 1)
   SCSI_UEXACT8 SCSI_HPTR sensePtr =
      (SCSI_UEXACT8 SCSI_HPTR) (SCSI_TARGET_UNIT(hiob)->senseBuffer);
#endif /* SCSI_TASK_SWITCH_SUPPORT != 1 */

   /* Another check condition may have occured using the same data path      */
   /* causing another check condition ARP int prior to command complete.  If */
   /* so data is moved at that time.  If sense data for this SCB already     */
   /* moved then this SCB number will not match any SCSI_SPI_STATUS_IU_SCB.  */

   /* Find status buffer that request sense data went into*/   
   for (i=0;i<2;i++)
   {
      if  (hiob->scbNumber ==  hhcb->SCSI_SPI_STATUS_IU_SCB[i]) 
      {
         break;   /* request sense data still in status buffer [i] */
      }
   
   }    
   
   if (i>1)
   {
      /*SCB number did not match iu buffer scb number*/
      return;
   }
   
   if (i)
   {
      statusPtr += SCSI_MAX_STATUS_IU_SIZE;    /*Point to mode 1 data address*/
   }
   
   SCSI_SET_NULL_SCB(hhcb->SCSI_SPI_STATUS_IU_SCB[i]);
   
   /* Invalidate CPU cache for the pre-allocated status buffer that has the  */
   /* request sense data that has been DMAed from the hardware.  This is to  */
   /* make sure that all the bytes that are referenced inside this buffer are*/
   /* taken from the physical memory and not from the CPU cache. */
   SCSI_INVALIDATE_CACHE(statusPtr, SCSI_MAX_STATUS_IU_SIZE);

   /* Get 'flag' field from the buffer which will be used further down. */
   SCSI_GET_LITTLE_ENDIAN8(hhcb, &flag, statusPtr, 2);

   /* Get 'status' field from the buffer and update the HIOB's trgstatus with*/
   /* what we get from the buffer.  This is the actual status of the target  */
   /* (like CHECK CONDITION, BUSY, QUEUE_FULL,etc.) */
   SCSI_GET_LITTLE_ENDIAN8(hhcb, &status, statusPtr, 3);
   hiob->trgStatus = status;

#if SCSI_DATA_IN_RETRY_DETECTION
   if (status == SCSI_UNIT_GOOD)
   {
      /* special case of target successfully performed a read  */
      /* retry operation. For certain RAID XOR implementations */
      /* we need to fail the I/O as the XOR may already have   */
      /* been performed on the data.                           */
      hiob->haStat = SCSI_HOST_READ_RETRY;
   }   
#endif /* SCSI_DATA_IN_RETRY_DETECTION */

   /* Get the length fields from the buffer. */
   SCSI_GET_LITTLE_ENDIAN8(hhcb, &senseLen, statusPtr, 7);
   SCSI_GET_LITTLE_ENDIAN8(hhcb, &pkLength, statusPtr, 11);

   /* check for packetized failure list length exist. Then handle */
   /* packetized failure code */
   statusPtr += 12;
   hiob->SCSI_IP_negoState = 0;

   if (status == SCSI_UNIT_CHECK) 
   {
      /* TMF flag (RSPValid) is valid case */
      if (flag & 0x01) 
      {
         for (i = 0; i < pkLength; i++)
         {
            SCSI_GET_LITTLE_ENDIAN8(hhcb, &pkStatus, statusPtr, i);
         }
 
         SCSI_GET_LITTLE_ENDIAN8(hhcb, &packetizeErrorCode, statusPtr, 3);
         hiob->tmfStatus = packetizeErrorCode;
         hiob->SCSI_IF_tmfValid = 1;    
         hiob->snsResidual = 0;        
         statusPtr += pkLength;         
      }

      /* Request sense (SNSValid) is valid case */
      if (flag & 0x02)              
      {

         /*use normal req sense info only, and invalidate the TMF flag*/
         hiob->SCSI_IF_tmfValid = 0;     
                
         for (i = 0; i < senseLen; i++)
         {
            /* handle normal request sense info in here */
            SCSI_GET_LITTLE_ENDIAN8(hhcb, &pkStatus, statusPtr, i);
         }

         /* If the length of the Request Sense data in the buffer is greater */
         /* than what has been asked for in the HIOB, adjust the length to   */
         /* the one in the HIOB and put zero in 'snsResidual' HIOB field to  */
         /* indicate there is no residue. If the length is less than or equal*/
         /* to the HIOB's length  calculate the proper 'snsResidual'.        */
         if (senseLen > hiob->snsLength)
         {
            senseLen = hiob->snsLength;
            hiob->snsResidual = 0;
         }
         else
         {
            hiob->snsResidual = hiob->snsLength - senseLen;
         }

         if (hiob->SCSI_IF_autoSense)
         {
            OSDmemcpy(hiob->snsBuffer,statusPtr, senseLen);
         }
#if (SCSI_TASK_SWITCH_SUPPORT != 1)
         else
         {
            /* copy sense data to pre-allocated memory for non-autosense*/
            OSDmemcpy(sensePtr,statusPtr, senseLen);
         }
#endif /* SCSI_TASK_SWITCH_SUPPORT != 1 */
          
         hiob->SCSI_IP_negoState = 0;

#if SCSI_NEGOTIATION_PER_IOB
         /* If negotiation is forced, only then initiate both sync and wide */
         if (hiob->SCSI_IF_forceReqSenseNego)
         {
            SCSIhChangeNegotiation(targetUnit);
         }
#endif /* SCSI_NEGOTIATION_PER_IOB */                     
      }
   }
}
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT) */

/*********************************************************************
*
*  SCSIhBadSeq
*
*     Terminate SCSI command sequence because sequence that is illegal,
*     or if we just can't handle it.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 callerId
*
*  Remarks:       Assumes the caller has paused the ARP. Also,
*                 the caller is responsible for restoring HCNTRL.               
*                  
*********************************************************************/
void SCSIhBadSeq (SCSI_HHCB SCSI_HPTR hhcb,
                  SCSI_UEXACT8 callerId)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 saveMode;

   OSDDebugPrint(1,("SCSIhBadSeq %d", callerId));


#if !SCSI_BIOS_SUPPORT
   /* Save callerId parameter for reporting later. */
   hhcb->badSeqCallerId = callerId;
#endif /* SCSI_BIOS_SUPPORT */

   /* Save the current mode */
   saveMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   /* Set the mode to 3 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Checking for SCSI 3rd party reset */
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & SCSI_SCSIRSTI)
   {
      /* scsi reset interrupt */
      SCSIhIntSrst(hhcb);
      /* Set the mode to entry mode */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,saveMode);
      return;
   }

   /* Don't reset/abort if there is a SCSI bus free interrupt pending */   
   if (!((OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT)) & SCSI_SCSIINT)
         && (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & SCSI_BUSFREE)))
   {
      /* Set event reason field */
      hhcb->freezeEventReason = SCSI_AE_HA_RESET;
      SCSI_ASYNC_EVENT(hhcb, SCSI_AE_OSMFREEZE);      /* AEN call back to RSM layer */

#if SCSI_TARGET_OPERATION
      if (hhcb->SCSI_HF_initiatorMode)
      {
         /* Only reset SCSI bus if operating in initiator mode */
         if (SCSIhResetScsi(hhcb))
         {
            SCSI_hSETBUSHUNG(hhcb,1);
         }

         /* Delay resetDelay msecs after scsi reset for slow devices to settle down. */
         SCSIhDelayCount25us(hhcb, SCSI_hGETTIMERCOUNT(hhcb,(hhcb->resetDelay*1000)));
      }
#else
      if (SCSIhResetScsi(hhcb))
      {
         SCSI_hSETBUSHUNG(hhcb,1);
      }

      /* Delay resetDelay msecs after scsi reset for slow devices to settle down. */
      SCSIhDelayCount25us(hhcb, SCSI_hGETTIMERCOUNT(hhcb,(hhcb->resetDelay*1000)));

#endif /* SCSI_TARGET_OPERATION */

      SCSIhResetChannelHardware(hhcb);
      /* Block upper layers from issuing Establish Connection HIOBs */
      SCSI_hTARGETHOLDESTHIOBS(hhcb,1);
      SCSI_hABORTCHANNEL(hhcb, SCSI_HOST_PHASE_ERR);
      SCSI_hRESETSOFTWARE(hhcb);
      /* Unblock upper layers from issuing Establish Connection HIOBs */
      SCSI_hTARGETHOLDESTHIOBS(hhcb,0);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                    (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                    (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));

#if (SCSI_TRIDENT_WORKAROUND && SCSI_TARGET_OPERATION)
      /* Do not remove this code until we verify that it's not
       * required for U320 ASIC.
       */
      /* In Trident after the reset the sequencer does not turn
       * off the SPIOEN, which causes the chip to assert a spurious
       * 'REQ' during the selection phase if it responds as a target
       * after the reset.  This workaround will prevent that from
       * happening.
       */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL0),
                    (OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL0)) & ~SCSI_SPIOEN));
#endif /* (SCSI_TRIDENT_WORKAROUND && SCSI_TARGET_OPERATION) */

      /* clear interrupt */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),
                     SCSI_CLRSCSIINT | SCSI_CLRARPINT);

      OSD_SYNCHRONIZE_IOS(hhcb);

      if (callerId == (SCSI_UEXACT8)SCSI_BADSEQ_CALLID37)
      {
         /* This call was to handle a unexpected fatal interrupt. This
          * code is temporary until proper exception handling is
          * implemented for receipt of PCIINT, interrupts.
          * Set bus hung flag so that fatal OSMEvent is generated.
          */
         SCSI_hSETBUSHUNG(hhcb,1);
      }
      
      /* AEN to RSM - HA initiated Reset */
      SCSI_ASYNC_EVENT(hhcb, SCSI_AE_HA_RESET);
     
      SCSI_ASYNC_EVENT(hhcb, SCSI_AE_OSMUNFREEZE);      /* AEN call back to RSM layer */
   }
   else
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                    (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                    (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));
   }

   /* Set the mode to entry mode for now */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,saveMode);
}

/*********************************************************************
*
*  SCSIhAbortTarget
*
*     Abort current target
*
*  Return Value:  None
*             
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:   
*             
*********************************************************************/
void SCSIhAbortTarget (SCSI_HHCB SCSI_HPTR hhcb,
                       SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 scsidatl;
   SCSI_UEXACT8 savedMode;

   if (OSD_INEXACT8(SCSI_AICREG(SCSI_ARPINTCODE)) == SCSI_NO_ID_MSG)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE | SCSI_ATNO);
      scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
   }

   if (SCSIhWait4Req(hhcb) == SCSI_MOPHASE)
   {
      /* Save the current mode */
      savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

      /* restore original MODE value */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);

      /* SCB ptr valid? */
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb)+1)) !=
          (SCSI_UEXACT8)SCSI_NULL_SCB)
      {
         /* Set the mode to entry mode for now */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);

         if (hiob == SCSI_NULL_HIOB)
         {
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID09);
            return;
         }

#if !SCSI_DOWNSHIFT_MODE
#if !SCSI_ASPI_SUPPORT
         if (hiob->SCSI_IP_mgrStat == SCSI_MGR_ABORTINPROG)
         {
            /* For standard mode only, check if the target is reselected */
            /* while active abort is in execution queue or selecting a   */
            /* target.  Then we need to remove it. */
            SCSI_hREMOVEACTIVEABORT(hhcb, hiob);

            hiob->SCSI_IP_mgrStat = SCSI_MGR_ABORTED;
            return;
         }
#endif /* !SCSI_ASPI_SUPPORT */
#if !SCSI_ASPI_SUPPORT_GROUP1
         else if (hiob->SCSI_IP_mgrStat == SCSI_MGR_TR)
         {
            /* Abort any related HIOB(s) in active pointer array */
            SCSIhTargetReset(hhcb, hiob);
            return;
         }
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
#endif /* !SCSI_DOWNSHIFT_MODE  */

         /* if we had set the aborted bit in host memory during searching    */
         /* of new queue, then we would have set the mgrStat to MGR_DONE_ABT;*/
         /* in this case we do not have to do anything.  We simply return    */
         else if (hiob->SCSI_IP_mgrStat == SCSI_MGR_DONE_ABT)
         {
            return;
         }
      }

      /* Restore entry mode */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
   }
   SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID10);
}

/*********************************************************************
*
*  SCSIhIntSelto
*
*     Handle SCSI selection timeout
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Mode 3 on entry.             
*                  
*********************************************************************/
void SCSIhIntSelto (SCSI_HHCB SCSI_HPTR hhcb,
                    SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_UEXACT8 scsiID;
   SCSI_UEXACT8 modeSave;

#if (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)
   SCSI_UEXACT8 simode0;
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE) */

   if (hiob != SCSI_NULL_HIOB)
   { 
      deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
      scsiID = (SCSI_UEXACT8) SCSI_TARGET_UNIT(hiob)->scsiID;

#if !SCSI_DOWNSHIFT_MODE
      if (hiob->SCSI_IF_freezeOnError &&
          !(SCSI_TARGET_UNIT(hiob)->targetControl->freezeMap &
          SCSI_FREEZE_HIPRIEXISTS))
      {
         SCSI_hFREEZEHWQUEUE(hhcb,hiob,SCSI_AE_FREEZEONERROR_START);
      }
#endif /* !SCSI_DOWNSHIFT_MODE */

      /* Firmware mode specific handling. */
      SCSI_hUPDATEEXEQ(hhcb,hiob);

      /* Clear Busy Target Table entry. */
      SCSI_hTARGETCLEARBUSY(hhcb,hiob);

#if !SCSI_NEGOTIATION_PER_IOB
      /* Force negotiation as the sync and wide values are no more valid */
      if (SCSI_hWIDEORSYNCNEGONEEDED(deviceTable))
      {
         SCSIhUpdateNeedNego(hhcb,scsiID);
      }
      else
      {
#if SCSI_BIOS_SUPPORT
         /* Do not update deviceTable if BIOS run time.*/
         if (!hhcb->SCSI_HF_biosRunTimeEnabled)
#endif /* SCSI_BIOS_SUPPORT */         
         {
            deviceTable->SCSI_DF_needNego = 0;
         }
         SCSIhAsyncNarrowXferRateAssign(hhcb,scsiID);
      }
#endif /* !SCSI_NEGOTIATION_PER_IOB */
   }

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ0),
       (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & ~(SCSI_ENSELO)));

   /* Save mode */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

#if (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)

   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      /* Harpoon2 Rev A chip has the following problem in the I/O cell:      */
      /*    When the SCSI bus is in LVD mode the I/O cell sampling module    */
      /*    does not get initialized properly.  The workaround is to enable  */
      /*    the bypass after a power-on chip reset and on an IOERR interrupt */
      /*    and disable the bypass after the first selection done or         */
      /*    selection timeout. The details of this hardware problem can be   */
      /*    found in the Razor database entry #491.                          */
      /* Here we disable the bypass as well as the selection done interrupt  */
      /* enable so that there will not be any future selection done          */
      /* interrupts. */

      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

      simode0 = OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE0));

      if ((simode0 & SCSI_ENSELDO)
#if SCSI_TARGET_OPERATION
                ||
          ((simode0 & SCSI_ENSELDI) && (hhcb->SCSI_HF_targetMode))
#endif /* SCSI_TARGET_OPERATION */
         )
      {
         simode0 &= ~SCSI_ENSELDO;
#if SCSI_TARGET_OPERATION
         if (hhcb->SCSI_HF_targetMode)
         {
            simode0 &= ~SCSI_ENSELDI;
         }
#endif /* SCSI_TARGET_OPERATION */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE0),simode0);
      }

      /* Disable the bypass if the SCSI bus is in LVD mode */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

      if (OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL)) & SCSI_ENAB40)
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DSPDATACTL),
            OSD_INEXACT8(SCSI_AICREG(SCSI_DSPDATACTL)) &
                         ~SCSI_BYPASSENAB);
      }
   }

#endif /* (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE) */

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRSELTIMO + SCSI_CLRBUSFREE);

   /* turn off LED for Non-Legacy hardware  */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT0), SCSI_CLRSELINGO); 
   
   if (hiob != SCSI_NULL_HIOB)
   {  
#if SCSI_SELTO_PER_IOB
      /* If seltoPerIOB is enabled, restore original values to timer */
      if (hiob->seltoPeriod > 0)
      {                                                                      
         /* Restore STIM field */
         SCSIhSetSeltoDelay(hhcb);

         /* Restore ALTSTIM field */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ1), 
            (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ1)) & ~SCSI_ALTSTIM));                          
      }      
#endif /* SCSI_SELTO_PER_IOB */
   
      hiob->haStat = SCSI_HOST_SEL_TO;

      SCSIhTerminateCommand(hiob);
   }
   else
   {
      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID11);
   }

   return;
}

/*********************************************************************
*
*  SCSIhIntFree
*
*     Acknowledge and clear SCSI Bus Free interrupt
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:                
*                  
*********************************************************************/
void SCSIhIntFree (SCSI_HHCB SCSI_HPTR hhcb,
                   SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 originalMode, busFreeMode;
   SCSI_UEXACT16 scbNumber;
#if !SCSI_PARITY_PER_IOB
   SCSI_UEXACT8 ubyteWork;
#endif /* !SCSI_PARITY_PER_IOB */

#if SCSI_PACKETIZED_IO_SUPPORT 
   /* Check if the unexpected bus free happened while sending a LQ/CMD     */
   /* packet pair. */
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_LQOSTAT1)) & (SCSI_LQOBUSFREE1))
   {
      /* SCSI bus went bus-free before a complete LQ/CMD packet pair could   */
      /* be sent to the target. Simply clear the interrupt and unpause the   */
      /* sequencer. Since the 'SELDO' will also be set by the hardware in    */
      /* this case and the 'waiting_SCB' will point to the SCB that got the  */
      /* unexpected bus free, the sequencer will detect that and requeue     */
      /* that SCB (and all the ones linked behind that) back in to the       */
      /* execution queue to be resent.                                       */
      /* If the bus goes free right after the 1st REQ of the LQ or the CMD   */
      /* packet but before the ACK, or after the LQ packet is done but       */
      /* before the 1st REQ for the CMD packet, the LQO Manager does not go  */
      /* the IDLE state.  To take care of these situations set the LQOTOIDLE */
      /* to make the LQO Manager go back to the IDLE state. */

      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

      /* Put the LQO Manager in the IDLE state to cover the conditions */
      /* mentioned above. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQCTL2),
         (OSD_INEXACT8(SCSI_AICREG(SCSI_LQCTL2)) | SCSI_LQOTOIDLE));

      /* Clear the interrupt condition. */
      /* Harpoon2 Rev A3 has a problem that self-clearing feature does not   */
      /* work for all the bits in CLRLQOINT1 register.  The details of this  */
      /* hardware problem can be found in the Razor database entry #663.     */
      /* To workaround this problem the software needs to do an extra write  */
      /* with value 0 to the corresponding bits after writing the bits to    */
      /* clear the interrupts in the LQOSTAT1 register.                      */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRLQOINT1), SCSI_CLRLQOBUSFREE1);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRLQOINT1), 0x00);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRBUSFREE);

      /* Since the Sequencer retries the command, we need to make sure that  */
      /* we do the following:                                                */
      /*    a) Make sure that the WAITING_SCB register points to the SCB that*/
      /*       has failed, and                                               */
      /*    b) Do not clear the SELDO, as this is needed for the sequencer   */
      /*       to get into the selection done path and retry the command.    */

      /* Since the WAITNG_SCB is already pointing to the SCB that has        */
      /* failed, we do not have to do anything more for the Rev B chip.  But */
      /* Rev A chip has a problem in updating CURRSCB after a selection      */
      /* is done.  Therefore, we need to update the FIRST_SCB, NEXT_SCB and  */
      /* LAST_SCB as the sequencer will not be able to do this. */

#if (!SCSI_DCH_U320_MODE && (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)) 
      if (SCSI_hHARPOON_REV_A(hhcb))
      {
         /* Rev A chip.  Apply the workaround mentioned above. */
#if SCSI_STANDARD_U320_MODE
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_FIRST_SCB),
               OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_FIRST_SCB+1),
               OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1)));
#endif /* SCSI_STANDARD_U320_MODE */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_LASTSCB0),
               OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_LASTSCB1),
               OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1)));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEXTSCB0),
               OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEXTSCB1),
               OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1)));
      }
#endif /* (!SCSI_DCH_U320_MODE && (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE))*/

#if SCSI_STANDARD_ENHANCED_U320_MODE
      if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320)
      {
         /* Since STANDARD_ENHANCED mode sequencer does not retry the command*/
         /* itself, we need to turn off SELDO and turn on ENSELO and then    */
         /* unpause the sequencer.  Sequencer would never know that an       */
         /* unexpected bus free had happened and it will continue waiting for*/
         /* SELDO. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT0), SCSI_CLRSELDO);  
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ0), SCSI_ENSELO);
      }
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

      return;
   }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

   if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
   {
      /* Harpoon2 Rev A has a problem wherein it reports even the expected   */
      /* bus free as unexpected during packetized connection.  Since the     */
      /* software workaround is very costly, the hardware had no choice but  */
      /* to turn off all bus free detection (either expected or unexpected). */
      /* But the chip still generates BUSFREE interrupt occasionally during  */
      /* packetized I/O test.  The workaround for this is to simply clear    */
      /* the interrupt when this spurious interrupt happens and restart the  */
      /* sequencer. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

      if (!(OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE1)) & SCSI_ENBUSFREE))
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRBUSFREE);

         return;
      }
   }

#if SCSI_BIOS_SUPPORT
   SCSIHReadScratchRam(hhcb,
                       (SCSI_UEXACT8 SCSI_SPTR) &originalMode,
                       (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ORIGINALMODE),
                       (SCSI_UEXACT16) 1);
#else /* !SCSI_BIOS_SUPPORT */
   originalMode = hhcb->SCSI_HP_originalMode;
#endif /* SCSI_BIOS_SUPPORT */

   /* Check if the unexpected bus free happened while doing the data xfer   */
   /* If so, clean up the mode (which would be either 0 or 1);              */
   /* If not, bus free has happened during a non-packetized and non-data    */
   /* phase.  */

   /* Did bus free happen while transferring data in FIFO 0 or FIFO 1? */
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT2)) & BFREETIME0)
   {
      /* Yes.  Find out the Mode that resulted in the bus free. */
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT2)) & BFREETIME1)
      {
         busFreeMode = SCSI_MODE1;
      }
      else
      {
         busFreeMode = SCSI_MODE0;
      }

      SCSI_hSETMODEPTR(hhcb,scsiRegister,busFreeMode);

      /* Get the HIOB that has failed. */
      scbNumber =
         ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb)+1))) << 8) |
         (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb))));

      if (SCSI_IS_NULL_SCB(scbNumber))
      {
         hiob = SCSI_NULL_HIOB;
      }
      else
      {
         hiob = SCSI_ACTPTR[scbNumber];
      }

      /* Clean up the Mode.  The clean up code is exactly same as what the   */
      /* sequencer does if it were put in the IDLE_LOOP_ENTRY. */

      /* Clear the RET_ADDR and ACTIVE_SCB sequencer variables to indicate*/
      /* that the SCB is not active anymore for this mode. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_hRET_ADDR(hhcb)+1),(SCSI_UEXACT8)0xFF);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb)+1),
         (SCSI_UEXACT8)SCSI_NULL_SCB);

      /* A couple of workarounds for the PCI-X problems both need to be   */
      /* added here to shutdown the data path gracefully. */

      /* Kill any S/G fetch or request to fetch pending. */
#if SCSI_DOWNSHIFT_MODE
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_DU320_SG_STATUS)) & SCSI_CCSGEN)
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSGCTL), (SCSI_UEXACT8)0);
      }
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_DU320_SG_STATUS)) &
            SCSI_DU320_HAVE_SG_CACHE)
      {
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_SCRATCH_FLAGS),
            OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_SCRATCH_FLAGS)) |
            (SCSI_UEXACT8)SCSI_DU320_SG_CACHE_AVAIL);
      }
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DU320_SG_STATUS),
         OSD_INEXACT8(SCSI_AICREG(SCSI_DU320_SG_STATUS)) & 
         ~(SCSI_UEXACT8)(SCSI_CCSGEN | SCSI_SG_FETCH_REQ |
           SCSI_DU320_HAVE_SG_CACHE));
#else /* SCSI_STANDARD_MODE */
#if SCSI_STANDARD_U320_MODE
      if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320)
      {
      
         if (OSD_INEXACT8(SCSI_AICREG(SCSI_SU320_SG_STATUS)) & SCSI_CCSGEN)
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSGCTL), (SCSI_UEXACT8)0);
         }
         if (OSD_INEXACT8(SCSI_AICREG(SCSI_SU320_SG_STATUS)) &
               SCSI_SU320_HAVE_SG_CACHE)
         {
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SCRATCH_FLAGS),
               OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SCRATCH_FLAGS)) |
               (SCSI_UEXACT8)SCSI_SU320_SG_CACHE_AVAIL);
         }
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SU320_SG_STATUS),
            OSD_INEXACT8(SCSI_AICREG(SCSI_SU320_SG_STATUS)) & 
            ~(SCSI_UEXACT8)(SCSI_CCSGEN | SCSI_SG_FETCH_REQ |
            SCSI_SU320_HAVE_SG_CACHE));
      }
      else
#else /* SCSI_STANDARD_ENHANCED_U320_MODE */
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSGCTL), (SCSI_UEXACT8)0);
      }
#endif /* SCSI_STANDARD_U320_MODE */
#endif /* SCSI_DOWNSHIFT_MODE */

      /* Clear the data path. This will free the data path, causing the     */
      /* hardware to allocate the data path for the next packetized I/O that*/
      /* may be ready for a data transfer. If we are very quick to come to  */
      /* point to do the clear channel, the very next packetized IO could   */
      /* make the hardware allocate this data path.  Otherwise the hardware */
      /* would allocate the other data path for the first packetized IO     */
      /* after the bus free and allocate this data path for the second IO.  */
      /* Therefore we should not touch the data path in any ways after we   */
      /* have done the clear channel. All the clean up should have been done*/
      /* before we reach here. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL), 
         (OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL)) | SCSI_CLRCHN)); 

      /* If the sequencer was doing something with the same mode as the one */
      /* that had the unexpected bus free condition, simply put the         */
      /* sequencer back at the IDLE_LOOP_TOP entry point. If it was working */
      /* with any other mode, we do not have to disturb the sequencer and it*/
      /* can be unpaused from whereever it got paused. */
      if (originalMode == busFreeMode)
      {
         /* Since the mode becomes free after we do clear channel, hardware  */
         /* can allocate the mode for a packetzied connection after the bus  */
         /* goes BUS-FREE.  This means that a CONFIG4DATA interrupt could be */
         /* pending for the sequencer when we unpause.  If we unpause the    */
         /* sequencer at the IDLE_LOOP_ENTRY this is what would happen:      */
         /*    - the sequencer would jump into its ISR to handle the         */
         /*      CONFIG4DATA interrupt where it enables the DMA and assigns  */
         /*      the RET_ADDR to the relevant routine to return back to.     */
         /*    - the sequencer then executes the first instruction at the    */
         /*      IDLE_LOOP_ENTRY which is to invalidate the RET_ADDR, which  */
         /*      is wrong.                                                   */
         /* We need to put the sequencer at the IDLE_LOOP_TOP entry to avoid */
         /* the above problem.  Note that this entry point is defined for    */
         /* only the modes 0 and 1.  For other modes we still need to put    */
         /* the sequencer at IDLE_LOOP_ENTRY point.                          */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                       (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_TOP(hhcb) >> 2));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                       (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_TOP(hhcb) >> 10));
      }
      else
      {
         /* No need to redirect the sequencer entry, if we reach here, as the*/
         /* sequencer is working with a mode other than the one that had the */
         /* unexpected bus free. */
      }
   }
   else
   {
      /* No.  Bus free has happened during a non-packetized and a non-data  */
      /* phase. */

      /* Bus free must have happened during a SELECTION, CMD, MSG, or STATUS */
      /* phase.  Since the sequencer deals with these scsi phases in Mode 3, */
      /* we can safely assume that the bus free has happened for the SCB     */
      /* pointed by the Mode 3 ACTIVE_SCB register. We can also assume that  */
      /* all the following statements are true:                              */
      /*    1. If the sequencer was in Mode 3 when the bus free interrupt    */
      /*       happened, we can assume that it was working on the SCB that   */
      /*       had the bus free.  Because, the ACTIVE_SCB in Mode 3 is       */
      /*       changed only by the sequencer and since the sequencer gets    */
      /*       paused when an unexpected bus free interrupt happens, it could*/
      /*       not have changed the Mode 3 ACTIVE_SCB.                       */
      /*    2. Even if the chip engages in a packetized I/O that follows the */
      /*       bus free, it would not change the mode to either 0 or 1 until */
      /*       the sequencer gets unpaused.                                  */
      /*    3. The Mode 3 ACTIVE_SCB may not be valid in which case we can   */
      /*       assume that the bus free happened before a nexus could be     */
      /*       established and therefore we do not have to do anything.      */

      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

      /* Get the HIOB that has failed. */
      scbNumber =
         ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb)+1))) << 8) |
         (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb))));

      if (SCSI_IS_NULL_SCB(scbNumber))
      {
         hiob = SCSI_NULL_HIOB;
      }
      else
      {
         hiob = SCSI_ACTPTR[scbNumber];
      }

      /* If the mode when bus free interrupt happened was Mode 3, we can     */
      /* simply put the sequencer at the idle loop. */
      if (originalMode == SCSI_MODE3)
      {
         /* Simply put the sequencer at the IDLE_LOOP_ENTRY point. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                       (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                       (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));
      }
      else
      {
         /* Clear Bus Free so BadSeq will reset */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRBUSFREE);

         /* Assume we will never come here. */
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID59);
         return;
      }
   }
   
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRBUSFREE);

   if (hiob != SCSI_NULL_HIOB)
   {
#if !SCSI_DOWNSHIFT_MODE
      if (hiob->SCSI_IF_freezeOnError &&
          !(SCSI_TARGET_UNIT(hiob)->targetControl->freezeMap &
          SCSI_FREEZE_HIPRIEXISTS))
      {
         SCSI_hFREEZEHWQUEUE(hhcb,hiob,SCSI_AE_FREEZEONERROR_START);
      }
#endif /* !SCSI_DOWNSHIFT_MODE */

      /* @REVISIT - Call to SCSI_hUPDATEEXEQ is removed for now. The only */
      /* case where this macro should be called in this routine is when   */
      /* the device goes unexpected bus free during the selection phase   */
      /* (and probably prior to the sequencer mending the queue after     */
      /* selection done). Possibily, testing SELINGO will be part of the  */
      /* solution.                                                        */

      /* Firmware mode specific handling.                                 */
      /* if (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & SCSI_SELINGO)       */
      /* {                                                                */
      /*    SCSI_hUPDATEEXEQ(hhcb,hiob);                                  */
      /* }                                                                */

      /* Clear Busy Target Table entry. */
      SCSI_hTARGETCLEARBUSY(hhcb,hiob);

      /* if the haStat is SCSI_HOST_DETECTED_ERR that means chim detect  */
      /* parity error where chim send abort msg to target and chim gets  */
      /* unexpected bus free after target goes bus free because sequecner*/
      /* hasn't had chance to disable bus free interrupt. Here we need to*/
      /* preserve the parity error status. This situation derives from   */
      /* implementation of handling Trident CRC end error and dual edge  */
      /* transfer error.                                                 */
      if (hiob->haStat != SCSI_HOST_DETECTED_ERR)
      {
         hiob->haStat = SCSI_HOST_BUS_FREE;
      }
#if !SCSI_PARITY_PER_IOB
      /* This is to handle the case that CHIM handles parity error first */
      /* and disable SCSI_ENSPCHK bit to turn parity checking off while  */
      /* target does not go to Message out phase instead target goes to  */
      /* bus free even though ATTN signal is asserted.  This results as  */
      /* host adapter's scsi parity checking feature is disabled as it   */
      /* should not be.  The reason is SCSIhHandleMsgOut routine is not  */
      /* called to turn SCSI_ENSPCHK bit back on.                        */
      /* The following logic is added to handle this situation.          */
      else
      {
         if (hhcb->SCSI_HF_scsiParity)
         {
            ubyteWork = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
            if (!(ubyteWork & SCSI_ENSPCHK))
            {
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), ubyteWork | SCSI_ENSPCHK);
            }
         }
      }
#endif /* !SCSI_PARITY_PER_IOB */

      SCSIhTerminateCommand(hiob);
   }
}

/*********************************************************************
*
*  SCSIhStandardU320RequestSense
*
*     Perform request sense for standard U320 mode
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Assumes busy target entry already cleared.
*
*********************************************************************/
#if SCSI_STANDARD_U320_MODE
void SCSIhStandardU320RequestSense (SCSI_HHCB SCSI_HPTR hhcb,
                                    SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UNIT_CONTROL SCSI_IPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_UEXACT8 i;

   /* set auto sense state */
   hiob->SCSI_IP_mgrStat = SCSI_MGR_AUTOSENSE;

   /* fresh SCB ram */
   for (i = 0; i < SCSI_SU320_DMAABLE_SIZE_SCB; i++)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+i), 0);
   }

   /* setup cdb contecnts */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,scontrol)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,starget)), (SCSI_UEXACT8)targetUnit->scsiID);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,slun)), (SCSI_UEXACT8)targetUnit->lunID);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,scdb_length)), 0x06);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,sg_cache_SCB)), SCSI_SU320_ONESGSEG);

   /* setup CDB in scb */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_scdb0)), 0x03);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_scdb1)),
                   (SCSI_UEXACT8) (targetUnit->lunID << 5));
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_scdb2)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_scdb3)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_scdb4)),
                   (SCSI_UEXACT8)hiob->snsLength);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_scdb5)), 0);
   
   /* do NOT have to setup s/g list pointer in scb */

   /* setup address of the first s/g element in scb */
   /* setup address of the first s/g element in scb */
   SCSI_hSETADDRESSSCB(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
        OSDoffsetof(SCSI_SCB_STANDARD_U320,saddress0)), hiob->snsBuffer);

#if SCSI_DOMAIN_VALIDATION
   if (hiob->SCSI_IF_dvIOB)
   {                          
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_STANDARD_U320,scontrol1)), SCSI_SU320_DV_ENABLE);
   }
#endif /* SCSI_DOMAIN_VALIDATION */

#if SCSI_SELTO_PER_IOB
   /* May require code if this compile option is supported. */
#endif /* SCSI_SELTO_PER_IOB*/

#if SCSI_RAID1
   /* don't mirror the request sense command */
   /* to prevent this, NULL the mirror_flag for SCSI_RAID1 enabled Sequencer */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,mirror_SCB)+1), SCSI_NULL_ENTRY);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,mirror_SCB)), 0);
#endif /* SCSI_RAID1 */
   /* setup count of the first s/g element in scb */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,slength0)), hiob->snsLength);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,slength1)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,slength2)), 0);
 
   /* Set the ATN length field to 1 indicating untagged and no
    * negotiation. This is updated later by callers of this
    * routine, if negotiation required.
    * Should the hiob->SCSI_IF_tagEnable flag be modified
    * to 0 also? - this might be an issue for the HWM would
    * need to restore the flag after R.S. finished.
    */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,atn_length)), 0x01);

   /* put it to head of execution queue */
   SCSIhStandardQHead(hhcb,hiob);
}
#endif /* SCSI_STANDARD_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardEnhU320RequestSense
*
*     Perform request sense for standard Enhanced U320 mode
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Assumes busy target entry already cleared.
*
*                 To be finished.
*
*********************************************************************/
#if SCSI_STANDARD_ENHANCED_U320_MODE
void SCSIhStandardEnhU320RequestSense (SCSI_HHCB SCSI_HPTR hhcb,
                                       SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UNIT_CONTROL SCSI_IPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_UEXACT8 i;

   /* set auto sense state */
   hiob->SCSI_IP_mgrStat = SCSI_MGR_AUTOSENSE;

   /* fresh SCB ram */
   for (i = 0; i < SCSI_SEU320_DMAABLE_SIZE_SCB; i++)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+i), 0);
   }

   /* setup cdb contecnts */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,scontrol)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,starget)), (SCSI_UEXACT8)targetUnit->scsiID);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,slun)), (SCSI_UEXACT8)targetUnit->lunID);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,scdb_length)), 0x06);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,sg_cache_SCB)), SCSI_SEU320_ONESGSEG);

   /* setup CDB in scb */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_scdb0)), 0x03);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_scdb1)),
                   (SCSI_UEXACT8) (targetUnit->lunID << 5));
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_scdb2)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_scdb3)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_scdb4)),
                   (SCSI_UEXACT8)hiob->snsLength);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_scdb5)), 0);
   
   /* do NOT have to setup s/g list pointer in scb */

   /* setup address of the first s/g element in scb */
   /* setup address of the first s/g element in scb */
   SCSI_hSETADDRESSSCB(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
        OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,saddress0)), hiob->snsBuffer);

#if SCSI_DOMAIN_VALIDATION
   if (hiob->SCSI_IF_dvIOB)
   {                          
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,scontrol1)), SCSI_SEU320_DV_ENABLE);
   }
#endif /* SCSI_DOMAIN_VALIDATION */

#if SCSI_SELTO_PER_IOB
   /* May require code if this compile option is supported. */
#endif /* SCSI_SELTO_PER_IOB*/

#if SCSI_RAID1
   /* don't mirror the request sense command */
   /* to prevent this, NULL the mirror_flag for SCSI_RAID1 enabled Sequencer */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,mirror_SCB)+1), SCSI_NULL_ENTRY);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,mirror_SCB)), 0);
#endif /* SCSI_RAID1 */
   /* setup count of the first s/g element in scb */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,slength0)), hiob->snsLength);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,slength1)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,slength2)), 0);
 
   /* Set the ATN length field to 1 indicating untagged and no
    * negotiation. This is updated later by callers of this
    * routine, if negotiation required.
    * Should the hiob->SCSI_IF_tagEnable flag be modified
    * to 0 also? - this might be an issue for the HWM would
    * need to restore the flag after R.S. finished.
    */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,atn_length)), 0x01);

   /* put it to head of execution queue */
   SCSIhStandardQHead(hhcb,hiob);
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/*********************************************************************
*
*  SCSIhDchU320RequestSense
*
*     Perform request sense for standard U320 mode
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Assumes busy target entry already cleared.
*
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320RequestSense (SCSI_HHCB SCSI_HPTR hhcb,
                               SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UNIT_CONTROL SCSI_IPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_UEXACT8 i;

   /* set auto sense state */
   hiob->SCSI_IP_mgrStat = SCSI_MGR_AUTOSENSE;

   /* fresh SCB ram */
   for (i = 0; i < SCSI_DCHU320_DMAABLE_SIZE_SCB; i++)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+i), 0);
   }

   /* setup cdb contecnts */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,scontrol)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,starget)), (SCSI_UEXACT8)targetUnit->scsiID);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,slun)), (SCSI_UEXACT8)targetUnit->lunID);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,scdb_length)), 0x06);
   OSD_OUTEXACT16_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,sg_cache_SCB)), SCSI_DCHU320_ONESGSEG);

   /* setup CDB in scb */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_scdb0)), 0x03);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_scdb1)),
                   (SCSI_UEXACT8) (targetUnit->lunID << 5));
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_scdb2)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_scdb3)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_scdb4)),
                   (SCSI_UEXACT8)hiob->snsLength);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_scdb5)), 0);
   
   /* do NOT have to setup s/g list pointer in scb */

   /* setup address of the first s/g element in scb */
   /* setup address of the first s/g element in scb */
   SCSI_hSETADDRESSSCB(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
        OSDoffsetof(SCSI_SCB_DCH_U320,saddress0)), hiob->snsBuffer);

   /* 
   NOTE: Removed this conditional to prevent sequencer from processing 
         this operation as if it had a mirrored SCB. The clearing of the
         mirror_SCB field took place at the beginning of this routine.
   */

/* #if SCSI_RAID1  $$ DCH $$  */

   /* don't mirror the request sense command */
   /* to prevent this, NULL the mirror_flag for SCSI_RAID1 enabled Sequencer */
   
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,mirror_SCB)+1), SCSI_NULL_ENTRY);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,mirror_SCB)), 0);
/* #endif */ /* SCSI_RAID1  $$ DCH $$ */

   /* setup count of the first s/g element in scb */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,slength0)), hiob->snsLength);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,slength1)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,slength2)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,slength3)), 0);
 
   /* Set the ATN length field to 1 indicating untagged and no
    * negotiation. This is updated later by callers of this
    * routine, if negotiation required.
    * Should the hiob->SCSI_IF_tagEnable flag be modified
    * to 0 also? - this might be an issue for the HWM would
    * need to restore the flag after R.S. finished.
    */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,atn_length)), 0x01);

   /* put it to head of execution queue */
   SCSIhDchU320QHead(hhcb,hiob);
}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhDownshiftU320RequestSense
*
*     Perform request sense for downshift U320 mode
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Assumes busy target entry already cleared.
*
*********************************************************************/
#if SCSI_DOWNSHIFT_U320_MODE
void SCSIhDownshiftU320RequestSense (SCSI_HHCB SCSI_HPTR hhcb,
                                     SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UNIT_CONTROL SCSI_IPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_UEXACT8 i;

   /* set auto sense state */
   hiob->SCSI_IP_mgrStat = SCSI_MGR_AUTOSENSE;

   /* fresh SCB ram */
   for (i = 0; i < SCSI_DU320_DMAABLE_SIZE_SCB; i++)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+i), 0);
   }

   /* setup cdb contecnts */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,scontrol)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,starget)), (SCSI_UEXACT8)targetUnit->scsiID);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,slun)), (SCSI_UEXACT8)targetUnit->lunID);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,scdb_length)), 0x06);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,sg_cache_SCB)), SCSI_DU320_ONESGSEG);

   /* setup CDB in scb */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_scdb0)), 0x03);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_scdb1)),
                   (SCSI_UEXACT8) (targetUnit->lunID << 5));
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_scdb2)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_scdb3)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_scdb4)),
                   (SCSI_UEXACT8)hiob->snsLength);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_scdb5)), 0);

   /* do NOT have to setup s/g list pointer in scb */

   /* setup address of the first s/g element in scb */
   SCSI_hSETADDRESSSCB(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
        OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,saddress0)), hiob->snsBuffer);

   /* setup count of the first s/g element in scb */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,slength0)), hiob->snsLength);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,slength1)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,slength2)), 0);
 
   /* Set the ATN length field to 1 indicating untagged and no
    * negotiation. This is updated later by callers of this
    * routine, if negotiation required.
    * Should the hiob->SCSI_IF_tagEnable flag be modified
    * to 0 also? - this might be an issue for the HWM would
    * need to restore the flag after R.S. finished.
    */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,atn_length)), 0x01);

   /* put it to head of execution queue */
   SCSIhDownshiftU320QHead(hhcb,hiob);
}
#endif /* SCSI_DOWNSHIFT_U320_MODE */


/*********************************************************************
*
*  SCSIhDownshiftEnhU320RequestSense
*
*     Perform request sense for downshift enhanced U320 mode
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Assumes busy target entry already cleared.
*
*********************************************************************/
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
void SCSIhDownshiftEnhU320RequestSense (SCSI_HHCB SCSI_HPTR hhcb,
                                     SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UNIT_CONTROL SCSI_IPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_UEXACT8 i;

   /* set auto sense state */
   hiob->SCSI_IP_mgrStat = SCSI_MGR_AUTOSENSE;

   /* fresh SCB ram */
   for (i = 0; i < SCSI_DEU320_DMAABLE_SIZE_SCB; i++)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+i), 0);
   }

   /* setup cdb contecnts */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,scontrol)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,starget)), (SCSI_UEXACT8)targetUnit->scsiID);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,slun)), (SCSI_UEXACT8)targetUnit->lunID);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,scdb_length)), 0x06);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,sg_cache_SCB)), SCSI_DEU320_ONESGSEG);

   /* setup CDB in scb */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_scdb0)), 0x03);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_scdb1)),
                   (SCSI_UEXACT8) (targetUnit->lunID << 5));
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_scdb2)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_scdb3)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_scdb4)),
                   (SCSI_UEXACT8)hiob->snsLength);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_scdb5)), 0);

   /* do NOT have to setup s/g list pointer in scb */

   /* setup address of the first s/g element in scb */
   SCSI_hSETADDRESSSCB(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
        OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,saddress0)), hiob->snsBuffer);

   /* setup count of the first s/g element in scb */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,slength0)), hiob->snsLength);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,slength1)), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,slength2)), 0);

   /* Set the ATN length field to 1 indicating untagged and no
    * negotiation. This is updated later by callers of this
    * routine, if negotiation required.
    * Should the hiob->SCSI_IF_tagEnable flag be modified
    * to 0 also? - this might be an issue for the HWM would
    * need to restore the flag after R.S. finished.
    */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,atn_length)), 0x01);

   /* put it to head of execution queue */
   SCSIhDownshiftU320QHead(hhcb,hiob);
}
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */

/*********************************************************************
*
*  SCSIhTerminateCommand
*
*     Terminate the specified hiob
*
*  Return Value:  None
*                  
*  Parameters:    hiob
*
*  Remarks:                
*                  
*********************************************************************/
void SCSIhTerminateCommand (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_HHCB SCSI_HPTR hhcb = SCSI_TARGET_UNIT(hiob)->hhcb;
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
   SCSI_UEXACT8 scsiID = (SCSI_UEXACT8) SCSI_TARGET_UNIT(hiob)->scsiID;
#if SCSI_RAID1
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 raid1Term;
   SCSI_UEXACT8 savedMode;
#endif /* SCSI_RAID1 */
#if (!SCSI_NEGOTIATION_PER_IOB && !SCSI_ASPI_SUPPORT_GROUP1)
   SCSI_UEXACT8 hcntrl;
#endif /* !SCSI_NEGOTIATION_PER_IOB && !SCSI_ASPI_SUPPORT_GROUP1 */

#if SCSI_BIOS_SUPPORT
   SCSI_UEXACT8 databuffer[4];
   SCSI_UEXACT8 i;
#endif /* SCSI_BIOS_SUPPORT */
#if SCSI_RAID1
   if (hiob->SCSI_IF_raid1)
   {
      /* Save incoming mode */
      savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

      /* Switch to mode 3 - a known mode for SCBPTR access. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   

      /* Save SCBPTR register. */ 
      scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
         ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

      /* Switch to the mirror_SCB for the raid1 command */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->mirrorHiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->mirrorHiob->scbNumber>>8));
      /* Null out the mirror_SCB so sister will know this */
      /* command has completed */
#if SCSI_STANDARD_U320_MODE
      if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320)
      {
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_STANDARD_U320,mirror_SCB)+1),
             SCSI_NULL_ENTRY);
      }
#endif /* SCSI_STANDARD_U320_MODE */
#if SCSI_STANDARD_ENHANCED_U320_MODE
      if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320)
      {
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,mirror_SCB)+1),
             SCSI_NULL_ENTRY);
      }
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */
#if SCSI_DCH_U320_MODE
      if (hhcb->firmwareMode == SCSI_FMODE_DCH_U320)
      {
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DCH_U320,mirror_SCB)+1),
             SCSI_NULL_ENTRY);
      }
#endif /* SCSI_DCH_U320_MODE */

      /* Switch to hiob SCB */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber>>8));
      /* if our scb's mirror_SCB is nulled out */
      /* Then the sister has already completed */
      /* And is waiting on us to finish */
      /* We must now terminate the IO */
#if SCSI_STANDARD_U320_MODE
      if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320)
      {
         raid1Term = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_STANDARD_U320, mirror_SCB)+1));
      }
#endif /* SCSI_STANDARD_U320_MODE */
#if SCSI_STANDARD_ENHANCED_U320_MODE
      if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320)
      {
         raid1Term = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320, mirror_SCB)+1));
      }
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */
#if SCSI_DCH_U320_MODE
      if (hhcb->firmwareMode == SCSI_FMODE_DCH_U320)
      {
         raid1Term = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_DCH_U320, mirror_SCB)+1));
      }
#endif /* SCSI_DCH_U320_MODE */

      /* Restore mode 3 SCBPTR. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));

      /* Restore MODE_PTR register. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  

      if (raid1Term != SCSI_NULL_SCB)
      {
         return;
      }

      if (!hiob->SCSI_IF_raid1Primary)
      {
         hiob = hiob->mirrorHiob;
      }

      SCSI_ACTPTR[hiob->mirrorHiob->scbNumber] = SCSI_NULL_HIOB;
  
   }
#endif /* SCSI_RAID1 */

#if SCSI_STANDARD_MODE
   /* free the associated entry in active pointer array */
   SCSI_ACTPTR[hiob->scbNumber] = SCSI_NULL_HIOB;
   
#elif !SCSI_BIOS_SUPPORT   
   SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET] = SCSI_NULL_HIOB;
   
#else
   for (i = 0; i < 4; i++)
   {
      databuffer[i] = SCSI_NULL_ENTRY;
   }

   SCSIHWriteScratchRam(hhcb,
                        (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ACTIVE_HIOB),
                        (SCSI_UEXACT16) 4,
                        (SCSI_UEXACT8 SCSI_IPTR) &databuffer);
#endif /* SCSI_STANDARD_MODE */
#if SCSI_EFI_BIOS_SUPPORT
   if (hiob->cmd != SCSI_CMD_RESET_TARGET)
#endif /* SCSI_EFI_BIOS_SUPPORT */
      /* make sure proper status get passed back */
      SCSIhSetStat(hiob);

   if (hiob->cmd == SCSI_CMD_INITIATE_TASK)
   {
      /* Free the SCB descriptor - if required. */
      SCSI_hFREESCBDESCRIPTOR(hhcb,hiob);
      
      /* post it back to upper layer code */
      SCSI_COMPLETE_HIOB(hiob);
   }
   else
   {
      /* Check and set if scsi negotiation is needed for */
      /* a particular target after the bus device reset executed */
#if !SCSI_ASPI_SUPPORT_GROUP1
      if (hiob->cmd == SCSI_CMD_RESET_TARGET)
      {
#if !SCSI_NEGOTIATION_PER_IOB
         /* save the HCNTRL value */
         hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

         /* pause the chip if necessary */
         if (!(hcntrl & SCSI_PAUSEACK))
         {
            SCSIhPauseAndWait(hhcb);
         }

         /* At the completion of a Target Reset HIOB in packetized     */
         /* CHIM must abort any HIOBs pending at the target.           */
         /* These HIOBs are in the Active Pointer Array.  But, they    */
         /* are not in Sequencer Done queue, Done queue, New queue and */
         /* Target's Execution queue.  Since New queue and Target's    */
         /* Execution queue will not have any regular HIOBs because    */
         /* they are all queued up in Device queue in RSM layer.       */
#if (SCSI_PACKETIZED_IO_SUPPORT)
         if (SCSI_hISPACKETIZED(deviceTable))
         {
            SCSIhTargetReset(hhcb, hiob);
         }
#endif /* (SCSI_PACKETIZED_IO_SUPPORT) */

         /* At the completion of a Target Reset HIOB, the host adapter  */
         /* should run in async/narrow mode to the reset target.  Hence,*/
         /* CHIM must reset xfer rate parameters in the current rate    */
         /* entry and in hardware rate table for this target.           */
         SCSIhAsyncNarrowXferRateAssign(hhcb,scsiID);

         /* Check if negotiation is needed for this device on the next I/O. */
         if (SCSI_hWIDEORSYNCNEGONEEDED(deviceTable))
         {
            /* Set negotiation needed */
            SCSIhUpdateNeedNego(hhcb,scsiID);
         }
         else
         {
            /* No negotiation */
#if SCSI_BIOS_SUPPORT
            if (!hhcb->SCSI_HF_biosRunTimeEnabled)
#endif /* SCSI_BIOS_SUPPORT */
            {
               deviceTable->SCSI_DF_needNego = 0;
            }
         }

         /* restore HCNTRL if necessary */
         if (!(hcntrl & SCSI_PAUSEACK))
         {
            SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
         }
#endif /* !SCSI_NEGOTIATION_PER_IOB */
#if !SCSI_EFI_BIOS_SUPPORT
         /* Free the SCB descriptor - if required. */
         SCSI_hFREESCBDESCRIPTOR(hhcb,hiob);
#else
         hiob->stat = SCSI_SCB_COMP;
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_ASPI_BIOS_COMMAND),
             OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_ASPI_BIOS_COMMAND)) &
             ~(SCSI_DU320_TARGETRESET_BIT));
      
#endif  /* !SCSI_EFI_BIOS_SUPPORT */
      }
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
      /* post back special HIOB to upper layer code */
      SCSI_COMPLETE_SPECIAL_HIOB(hiob);
   }
}

/*********************************************************************
*
*  SCSIhSetStat
*
*     Set status value to MgrStat
*
*  Return Value:  None
*                  
*  Parameters:    hiob
*
*  Remarks:
*                  
*********************************************************************/
void SCSIhSetStat (SCSI_HIOB SCSI_IPTR hiob)
{
#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT)
   /* If the target is in packetized mode, and 'trgStatus' is            */
   /* SCSI_UNIT_CHECK, then get the Request Sense data from the internal */
   /* buffer. */
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
              (hiob->haStat == SCSI_HOST_ABT_TRG_RST) ||
              (hiob->haStat == SCSI_HOST_ABT_LUN_RST)) &&
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
      hiob->stat = SCSI_SCB_COMP;           /* Update status */
   }

#if SCSI_RAID1
   if (hiob->SCSI_IF_raid1)
   {
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
            /* Check for the terminate hiob->mirrorHiob due to the selection time-out, */
            /* unexpected busfree, or other target errors: queue full etc. */
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
         hiob->mirrorHiob->stat = SCSI_SCB_COMP;           /* Update status */
      }
   }
#endif /* SCSI_RAID1 */
}

/*********************************************************************
*
*  SCSIhBreakInterrupt
*
*     Trap for break interrupt
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       This routine will search through active pointer array
*                 for any HIOB need to be aborted or enqueue to head
*                 of the queue if it's Bus_Device_Reset HIOB.
*                  
*********************************************************************/
#if (!SCSI_BIOS_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
void SCSIhBreakInterrupt (SCSI_HHCB SCSI_HPTR hhcb,
                          SCSI_HIOB SCSI_IPTR hiobActive)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable; 

#if !SCSI_DOWNSHIFT_MODE
   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_UEXACT16 i;
#endif /* !SCSI_DOWNSHIFT_MODE */

#if SCSI_TARGET_OPERATION
   SCSI_HHCB SCSI_HPTR aborthhcb;
#endif /* SCSI_TARGET_OPERATION */

#if SCSI_TARGET_OPERATION
   OSDDebugPrint(8,("\n Entered SCSIhBreakInterrupt "));
#endif /* SCSI_TARGET_OPERATION */

   if (hhcb->SCSI_HP_expRequest)
   {
      if (hhcb->SCSI_HP_nonExpBreak)
      {
         /* Clear the idle loop0 break point interrupt */
         SCSI_hCLEARBREAKPOINT(hhcb, (SCSI_UEXACT8)SCSI_ENTRY_IDLE_LOOP0_BREAK);

         /* Have to restore expander status break */
         SCSI_hSETBREAKPOINT(hhcb, (SCSI_UEXACT8)SCSI_ENTRY_EXPANDER_BREAK);
      }
      else
      {
         if (hiobActive == SCSI_NULL_HIOB)
         {
            /* Clear the expander break point interrupt */
            SCSI_hCLEARBREAKPOINT(hhcb, (SCSI_UEXACT8)SCSI_ENTRY_EXPANDER_BREAK);

            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID14);
            return;
         }
         /* examine the expander status */
         deviceTable = SCSI_TARGET_UNIT(hiobActive)->deviceTable;
         deviceTable->SCSI_DF_behindExp = 
            ((OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT2)) & SCSI_EXP_ACTIVE) != 0);

         /* Clear the expander break point interrupt */
         SCSI_hCLEARBREAKPOINT(hhcb, (SCSI_UEXACT8)SCSI_ENTRY_EXPANDER_BREAK);
      }
   }
   else
   {
      /* Clear the idle loop0 break point interrupt */
      SCSI_hCLEARBREAKPOINT(hhcb, (SCSI_UEXACT8)SCSI_ENTRY_IDLE_LOOP0_BREAK);
   }

   if (hhcb->SCSI_HP_nonExpBreak)
   {
      /* clear non exp break flag */
      hhcb->SCSI_HP_nonExpBreak = 0;

      /* check if target mode DISABLE_ID is active */
      SCSI_hTARGETDISABLEID(hhcb);

#if !SCSI_DOWNSHIFT_MODE

      /* Search all HIOB in active pointer array */
      for (i = 0; i < hhcb->numberScbs; i++)
      {
         if ((hiob = SCSI_ACTPTR[i]) != SCSI_NULL_HIOB)
         {
            /* Make sure the request is for the current host adapter */
#if SCSI_TARGET_OPERATION
            aborthhcb = 
               (hiob->SCSI_IP_targetMode)? SCSI_NEXUS_UNIT(hiob)->hhcb : SCSI_TARGET_UNIT(hiob)->hhcb;
            if (aborthhcb == hhcb)
#else
            if (SCSI_TARGET_UNIT(hiob)->hhcb == hhcb)
#endif /* SCSI_TARGET_OPERATION */
            {
               if (hiob->SCSI_IP_mgrStat == SCSI_MGR_ABORTINREQ)
               {
                  SCSI_hACTIVEABORT(hhcb, hiob, SCSI_HOST_ABT_HOST);
               }
#if (SCSI_TARGET_OPERATION && SCSI_STANDARD_ENHANCED_U320_MODE)
#if (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
               else
               if ((hiob->SCSI_IP_mgrStat == SCSI_MGR_BADLQT_AUTORESPONSE) ||
                   (hiob->SCSI_IP_mgrStat == SCSI_MGR_LQOTCRC_AUTORESPONSE))
               {
                  /* Set mode pointer register to the current fifo */
                  SCSIhSetModeToCurrentFifo(hhcb);

                  /* Send to H/W */
                  OSDDebugPrint(8,("\nSCSIhBreakInterrupt: PIO to sequencer SCB# %xh ",
                                   hiob->scbNumber));
                  SCSI_hTARGETSENDHIOBSPECIAL(hhcb,hiob);

                  if (hiob->SCSI_IP_mgrStat == SCSI_MGR_BADLQT_AUTORESPONSE)
                  {
                     /* Set the MxBITBUCKET and DELAYSTAT bits to flush the command packet */
                     OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL), 
                                   (OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL)) |
                                    SCSI_M01DELAYSTAT | SCSI_M01BITBUCKET));
                  }
               }
               else
               if (hiob->SCSI_IP_mgrStat == SCSI_MGR_LQOATNLQ)
               {
                  /* Handle LQOATNLQ interrupt */
                  SCSIhTargetStandardEnhU320LqoAtnLq(hhcb,hiob);
               }
#endif /* (SCSI_PACKETIZED_IO_SUPPORT && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */
#endif /* (SCSI_TARGET_OPERATION && SCSI_STANDARD_ENHANCED_U320_MODE) */
            }
         }
      }
#endif /* !SCSI_DOWNSHIFT_MODE */
   }
}
#endif /* (!SCSI_BIOS_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */

/*********************************************************************
*
*  SCSIhStandardU320ActiveAbort
*
*     Abort the active HIOB for standard U320 mode sequencer.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 aborted hiob
*                 host adapter status
*
*  Remarks:       For the case of the target's disconnection, it might
*                 reconnected before the abort message HIOB has ever
*                 started or done on SCSI selection.  This issues is
*                 addressed in two places:
*                 1. In SCSIhAbortTarget routine will try to remove
*                    the HIOB from the internal sequencer Done queue.
*                 2. In SCSIhCmdComplete routine to not post it back.
*
*                 Assumes the sequencer is paused. 
*
*********************************************************************/
#if (SCSI_STANDARD_U320_MODE && !SCSI_ASPI_SUPPORT)
void SCSIhStandardU320ActiveAbort (SCSI_HHCB SCSI_HPTR hhcb,
                                   SCSI_HIOB SCSI_IPTR hiob,
                                   SCSI_UEXACT8 haStatus)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UINT32 i = 0;            /* time-out counter */
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 byteBuf;
   SCSI_UEXACT8 savedMode;

   /* Save incoming mode */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 2 - a known mode for Command Channel SCB RAM access. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);

   /* Make sure no SCB is in transit from the host to SCB ram and vice versa */
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL)) & (SCSI_CCARREN | SCSI_CCSCBEN))
   {
      while ((!(OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL)) & (SCSI_ARRDONE | SCSI_CCSCBDONE))) &&
             (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
      {
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }
   }

   /* If the SCB we are trying to abort is the one which is getting DMAed, */
   /* set the 'aborted' bit in the SCB in the SCB array so that it will    */
   /* never be fired by the sequencer.                                     */
   if (((OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL)) &
        (SCSI_CCSCBEN+SCSI_CCSCBDIR+SCSI_CCARREN)) ==
        (SCSI_CCSCBEN+SCSI_CCSCBDIR+SCSI_CCARREN)) &&
       (hiob->scbNumber ==
        ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBPTR))) |
         ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBPTR+1))) << 8))))
   {
      /* Set abort status */
      hiob->stat = SCSI_SCB_ABORTED;
      hiob->haStat = haStatus;

      /* Set abort request is done */
      hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE_ABT;

      /* Prepare scb to be aborted by setting 'aborted' bit in SCB ram */
   
      /* Switch to mode 3 - a known mode for SCBPTR access. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

      /* Save SCBPTR register. */ 
      scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
               ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      byteBuf = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                    OSDoffsetof(SCSI_SCB_STANDARD_U320, scontrol)));
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_STANDARD_U320, scontrol)),
                      (byteBuf | SCSI_SU320_ABORTED));
         
      /* Restore mode 3 SCBPTR. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));
   }

   /* If aborted HIOB is in New queue, sequencer Done queue or Done queue. */
   else if (!SCSIhStandardSearchNewQ(hhcb, hiob, haStatus) &&
            !SCSIhStandardSearchSeqDoneQ(hhcb, hiob) &&
            !SCSIhStandardSearchDoneQ(hhcb, hiob))
   {
#if SCSI_TARGET_OPERATION
      if (hiob->SCSI_IP_targetMode)
      {
         /* Only establish connection HIOBs may be aborted */
         if (hiob->cmd == SCSI_CMD_ESTABLISH_CONNECTION)
         {
            SCSIhTargetStandardU320EstActiveAbort(hhcb,hiob,haStatus);
         }
      }
      else
#endif /* SCSI_TARGET_OPERATION */
      /* Call appropriate routine for packetized/non-packetized mode */
#if SCSI_PACKETIZED_IO_SUPPORT
      if (SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))
      {
         SCSIhStandardU320PackActiveAbort(hhcb,hiob);
      }
      else
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
      {
         SCSIhStandardU320NonPackActiveAbort(hhcb,hiob);
      }
   }

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  
}
#endif /* SCSI_STANDARD_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardEnhU320ActiveAbort
*
*     Abort the active HIOB for standard enhanced U320 mode sequencer.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 aborted hiob
*                 host adapter status
*
*  Remarks:       For the case of the target's disconnection, it might
*                 reconnected before the abort message HIOB has ever
*                 started or done on SCSI selection.  This issues is
*                 addressed in two places:
*                 1. In SCSIhAbortTarget routine will try to remove
*                    the HIOB from the internal sequencer Done queue.
*                 2. In SCSIhCmdComplete routine to not post it back.
*
*                 Assumes the sequencer is paused. 
*
*********************************************************************/
#if (SCSI_STANDARD_ENHANCED_U320_MODE && !SCSI_ASPI_SUPPORT)
void SCSIhStandardEnhU320ActiveAbort (SCSI_HHCB SCSI_HPTR hhcb,
                                      SCSI_HIOB SCSI_IPTR hiob,
                                      SCSI_UEXACT8 haStatus)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UINT32 i = 0;            /* time-out counter */
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 byteBuf;
   SCSI_UEXACT8 savedMode;

   /* Save incoming mode */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 2 - a known mode for Command Channel SCB RAM access. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);

   /* Make sure no SCB is in transit from the host to SCB ram and vice versa */
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL)) & (SCSI_CCARREN | SCSI_CCSCBEN))
   {
      while ((!(OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL)) & (SCSI_ARRDONE | SCSI_CCSCBDONE))) &&
             (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
      {
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }
   }

   /* If the SCB we are trying to abort is the one which is getting DMAed, */
   /* set the 'aborted' bit in the SCB in the SCB array so that it will    */
   /* never be fired by the sequencer.                                     */
   if (((OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL)) &
        (SCSI_CCSCBEN+SCSI_CCSCBDIR+SCSI_CCARREN)) ==
        (SCSI_CCSCBEN+SCSI_CCSCBDIR+SCSI_CCARREN)) &&
       (hiob->scbNumber ==
        ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBPTR))) |
         ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBPTR+1))) << 8))))
   {
      /* Set abort status */
      hiob->stat = SCSI_SCB_ABORTED;
      hiob->haStat = haStatus;

      /* Set abort request is done */
      hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE_ABT;

      /* Prepare scb to be aborted by setting 'aborted' bit in SCB ram */
   
      /* Switch to mode 3 - a known mode for SCBPTR access. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

      /* Save SCBPTR register. */ 
      scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
               ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      byteBuf = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                    OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320, scontrol)));
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320, scontrol)),
                      (byteBuf | SCSI_SEU320_ABORTED));
         
      /* Restore mode 3 SCBPTR. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));
   }

   /* If aborted HIOB is in New queue, sequencer Done queue or Done queue. */
   else if (!SCSIhStandardSearchNewQ(hhcb, hiob, haStatus) &&
            !SCSIhStandardSearchSeqDoneQ(hhcb, hiob) &&
            !SCSIhStandardSearchDoneQ(hhcb, hiob))
   {
#if SCSI_TARGET_OPERATION
      if (hiob->SCSI_IP_targetMode)
      {
         /* Only establish connection HIOBs may be aborted */
         if (hiob->cmd == SCSI_CMD_ESTABLISH_CONNECTION)
         {
            SCSIhTargetStandardEnhU320EstActiveAbort(hhcb,hiob,haStatus);
         }
      }
      else
#endif /* SCSI_TARGET_OPERATION */
      /* Call appropriate routine for packetized/non-packetized mode */
#if (SCSI_PACKETIZED_IO_SUPPORT)
      if (SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))
      {
         SCSIhStandardEnhU320PackActiveAbort(hhcb,hiob);
      }
      else
#endif /* (SCSI_PACKETIZED_IO_SUPPORT) */
      {
         SCSIhStandardEnhU320NonPackActiveAbort(hhcb,hiob);
      }
   }

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/*********************************************************************
*
*  SCSIhDchU320ActiveAbort
*
*     Abort the active HIOB for DCH U320 mode sequencer.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 aborted hiob
*                 host adapter status
*
*  Remarks:       For the case of the target's disconnection, it might
*                 reconnected before the abort message HIOB has ever
*                 started or done on SCSI selection.  This issues is
*                 addressed in two places:
*                 1. In SCSIhAbortTarget routine will try to remove
*                    the HIOB from the internal sequencer Done queue.
*                 2. In SCSIhCmdComplete routine to not post it back.
*
*                 Assumes the sequencer is paused. 
*
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320ActiveAbort (SCSI_HHCB SCSI_HPTR hhcb,
                              SCSI_HIOB SCSI_IPTR hiob,
                              SCSI_UEXACT8 haStatus)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UINT32 i = 0;            /* time-out counter */
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 byteBuf;
   SCSI_UEXACT8 savedMode;

   /* Save incoming mode */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 2 - a known mode for Command Channel SCB RAM access. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);

   /* Make sure no SCB is in transit from the host to SCB ram and vice versa */
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL)) & (SCSI_CCARREN | SCSI_CCSCBEN))
   {
      while ((!(OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL)) & (SCSI_ARRDONE | SCSI_CCSCBDONE))) &&
             (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
      {
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }
   }

   /* If the SCB we are trying to abort is the one which is getting DMAed, */
   /* set the 'aborted' bit in the SCB in the SCB array so that it will    */
   /* never be fired by the sequencer.                                     */
   if (((OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL)) &
        (SCSI_CCSCBEN+SCSI_CCSCBDIR+SCSI_CCARREN)) ==
        (SCSI_CCSCBEN+SCSI_CCSCBDIR+SCSI_CCARREN)) &&
       (hiob->scbNumber ==
        ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBPTR))) |
         ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBPTR+1))) << 8))))
   {
      /* Set abort status */
      hiob->stat = SCSI_SCB_ABORTED;
      hiob->haStat = haStatus;

      /* Set abort request is done */
      hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE_ABT;

      /* Prepare scb to be aborted by setting 'aborted' bit in SCB ram */
   
      /* Switch to mode 3 - a known mode for SCBPTR access. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

      /* Save SCBPTR register. */ 
      scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
               ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      byteBuf = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                    OSDoffsetof(SCSI_SCB_DCH_U320, scontrol)));
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DCH_U320, scontrol)),
                      (byteBuf | SCSI_DCHU320_ABORTED));
         
      /* Restore mode 3 SCBPTR. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));
   }

   /* If aborted HIOB is in New queue, sequencer Done queue or Done queue. */
   else if (!SCSIhStandardSearchNewQ(hhcb, hiob, haStatus) &&
            !SCSIhDchU320SearchSeqDoneQ(hhcb, hiob) &&
            !SCSIhStandardSearchDoneQ(hhcb, hiob))
   {
#if SCSI_TARGET_OPERATION
      if (hiob->SCSI_IP_targetMode)
      {
         /* Only establish connection HIOBs may be aborted */
         if (hiob->cmd == SCSI_CMD_ESTABLISH_CONNECTION)
         {
            SCSIhTargetDchU320EstActiveAbort(hhcb,hiob,haStatus);
         }
      }
      else
#endif /* SCSI_TARGET_OPERATION */
      /* Call appropriated routine for packetized/non-packetized mode */
#if (SCSI_PACKETIZED_IO_SUPPORT)
      if (SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))
      {
         SCSIhDchU320PackActiveAbort(hhcb,hiob);
      }
      else
#endif /* (SCSI_PACKETIZED_IO_SUPPORT) */
      {
         SCSIhDchU320NonPackActiveAbort(hhcb,hiob);
      }
   }

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  
}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardU320NonPackActiveAbort
*
*     Abort the active HIOB for standard U320 mode sequencer
*     in non-packetized mode.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 aborted hiob
*
*  Remarks:       Assumes sequencer is paused.
*
*********************************************************************/
#if (SCSI_STANDARD_U320_MODE && !SCSI_ASPI_SUPPORT)
void SCSIhStandardU320NonPackActiveAbort (SCSI_HHCB SCSI_HPTR hhcb,
                                          SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT32 i;
   SCSI_UEXACT16 waitingScb;
   SCSI_UEXACT16 nextScbTargQ;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 byteBuf;
   SCSI_UEXACT8 savedMode;

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Switch to mode 1 - the mode that uses for non-packetized data transfer */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE1);

   /* If the aborted HIOB is currently active, then CHIM will   */
   /* have to wait until it is inactive or might be completed.  */
   /* CHIM will set the sequencer break point at the idle loop0 */
   /* again and wait.                                           */
   if ((hiob->scbNumber ==
        (((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SU320_ACTIVE_SCB+1))) << 8) |
          (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SU320_ACTIVE_SCB))))) &&
       ((OSD_INEXACT8(SCSI_AICREG(SCSI_CCSGCTL)) & SCSI_CCSGEN) ||
        (OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL)) & (SCSI_HDMAEN | SCSI_SCSIEN))))
   {
      /* IMPORTANT NOTE:                                                */
      /* THE BELOW WORKAROUND APPLIES TO HARPOON2 REV A ONLY.  IF THIS  */
      /* ROUTINE EVER COMBINES WITH STANDARD ENHANCED U320, THEN WE     */
      /* SHOULD HAVE A RUN-TIME CHECK FOR STANDARD U320 FIRMWARE MODE.  */
      /* AND IT MIGHT BE BETTER TO HAVE A CONDITIONAL COMPILE OPTION,   */
      /* SCSI_STANDARD_U320_MODE, TO SAVE CODE SPACE.                   */

      /* Harpoon2 A4 chips has a problem where we cannot set the same   */
      /* break point address after servicing it.  The system will hang  */
      /* after sequencer is unpaused.  The details issue can be found   */
      /* in the Razor database entry #662 (Harpoon 2 A4).  It is changed*/
      /* to #743 in Harpoon 2 Rev B.                                    */

      /* At this implementation, CHIM will single step the sequencer    */
      /* code inorder to setup the same break point address at          */
      /* sequencer idle loop0.                                          */
      /* Sequencer has been added a 'nop' instruction at the idle_loop0 */
      /* label for proper implementation below.  Because the instruction*/
      /* at the idle_loop0 label might change the mode pointer or       */
      /* something else.                                                */

      /* H2A4 WORKAROUND START */

      /* Clear break address interrupt */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRBRKADRINT);

      /* Setup to Single Step sequencer */
      byteBuf = (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_SEQCTL0)) & ~SCSI_BRKADRINTEN);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0), (byteBuf | SCSI_STEP));

      /* Unpause sequencer to execution current instruction */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_HCNTRL),
          (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & ~SCSI_PAUSE));

      /* Make sure the chip is paused before proceed */
      i = 0;
      while ((!(OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & SCSI_PAUSEACK)) &&
             (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
      {
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }    

      /* Clear Single Step sequencer */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0), byteBuf);

      /* H2A4 WORKAROUND END */

      /* Set the idle loop0 break point interrupt */
      SCSI_hSETBREAKPOINT(hhcb, (SCSI_UEXACT8)SCSI_ENTRY_IDLE_LOOP0_BREAK);

      /* Set flag to indicate this is a non expander break interrupt */
      hhcb->SCSI_HP_nonExpBreak = 1;
   }
   else
   {
      /* Switch to mode 3 - a known mode for most registers. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

      waitingScb =
         ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
         (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));

      /* If the aborted HIOB is currently a waiting SCB.                */
      /* A waiting SCB is a SCB has been programmed to select a target. */
      if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) &&
          (hiob->scbNumber == waitingScb))
      {
         /* Set aborted status is done. */
         /* The stat and haStat are set before breakpoint interrupt. */
         hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE_ABT;

         /* Prepare scb to be aborted by setting 'aborted' bit in SCB ram */

         /* Save mode 3 SCBPTR value. */
         scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
                  ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                       (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                       (SCSI_UEXACT8)(hiob->scbNumber >> 8));

         /* Set 'aborted' bit of 'scontrol' field in SCB ram. */
         /* Sequencer will check this bit when the selection  */
         /* is done and will send ABORT message to target.    */
         byteBuf = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                       OSDoffsetof(SCSI_SCB_STANDARD_U320, scontrol)));
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_STANDARD_U320,scontrol)),
             byteBuf | SCSI_SU320_ABORTED);

         /* Restore mode 3 SCBPTR. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));
      }

      /* If the aborted HIOB is currently in the following state:    */
      /*   1. Execution queue - waiting to be executed               */
      /*   2. Disconnection - pending at the target                  */

      /* The aborted HIOB will be posted back if it found in execution queue */
      else if (!SCSI_hSEARCHEXEQ(hhcb, hiob, hiob->haStat, 1))
      {
         /* The aborted HIOB has not been completed, we will issue    */
         /* the abort message to the target.  There is case that a    */
         /* target disconnected and never come back which will result */
         /* in selection time out.  On the other hand, the command    */
         /* will be executed and abort message will be sent to target.*/

         /* Prepare scb to be aborted via special function */

         /* Switch to mode 3 - a known mode for SCBPTR access. */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
         /* Save mode 3 SCBPTR value. */
         scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
                  ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                       (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                       (SCSI_UEXACT8)(hiob->scbNumber >> 8));

         /* Prepare scb to be aborted by setting 'aborted' bit and   */
         /* 'special function' bit of the scontrol flag in SCB ram   */
         /* The 'aborted' bit will be used during reselection.       */
         /* The 'special function' will force the sequencer to start */
         /* this SCB and send out the specified 'special_info' byte. */
         byteBuf = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                       OSDoffsetof(SCSI_SCB_STANDARD_U320, scontrol)));
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_STANDARD_U320,scontrol)),
             (byteBuf | SCSI_SU320_ABORTED | SCSI_SU320_SPECFUN));

         /* Setup special opcode - msg_to_targ */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_special_opcode)),
             SCSI_SU320_MSGTOTARG);

         /* Setup message byte - special_info */
         if ((hiob->SCSI_IF_tagEnable) && (!hiob->SCSI_IF_disallowDisconnect))
         {
            /* Abort tagged command - ABORT TASK message */
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_special_info)),
                SCSI_MSG0D);
         }
         else
         {
            /* Abort non-tagged command - ABORT TASK SET message */
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_special_info)),
                SCSI_MSG06);
         }

         /* If the head SCB in the specified target's execution queue */
         /* is selecting, then we will insert the aborted SCB onto    */
         /* the target's execution queue right next to the selecting  */
         /* SCB.  And re-link the target's execution queue.           */
         if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) &&
             (!SCSI_IS_NULL_SCB(waitingScb)) &&
             (SCSI_ACTPTR[waitingScb] != SCSI_NULL_HIOB) &&
             (SCSI_TARGET_UNIT(SCSI_ACTPTR[waitingScb])->hhcb == hhcb) &&
             (SCSI_TARGET_UNIT(SCSI_ACTPTR[waitingScb])->scsiID == SCSI_TARGET_UNIT(hiob)->scsiID))
         {
            /* Get the next exetarg SCB in the waiting SCB */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                          (SCSI_UEXACT8)waitingScb);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                          (SCSI_UEXACT8)(waitingScb >> 8));

            nextScbTargQ = 
               (SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                  OSDoffsetof(SCSI_SCB_STANDARD_U320, SCSI_SU320_q_exetarg_next)))) |
               ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                  OSDoffsetof(SCSI_SCB_STANDARD_U320, SCSI_SU320_q_exetarg_next)+1))) << 8);

            /* Insert the aborting SCB in the target's execution */
            /* queue to be next to the waiting SCB.              */
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                  OSDoffsetof(SCSI_SCB_STANDARD_U320,
                                              SCSI_SU320_q_exetarg_next)),
                               (SCSI_UEXACT8)hiob->scbNumber);
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                  OSDoffsetof(SCSI_SCB_STANDARD_U320,
                                              SCSI_SU320_q_exetarg_next)+1),
                               (SCSI_UEXACT8)(hiob->scbNumber >> 8));

            /* Link the aborting SCB to the next exetarg SCB */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                          (SCSI_UEXACT8)hiob->scbNumber);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                          (SCSI_UEXACT8)(hiob->scbNumber >> 8));

            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                  OSDoffsetof(SCSI_SCB_STANDARD_U320,
                                              SCSI_SU320_q_exetarg_next)),
                               (SCSI_UEXACT8)nextScbTargQ);
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                  OSDoffsetof(SCSI_SCB_STANDARD_U320,
                                              SCSI_SU320_q_exetarg_next)+1),
                               (SCSI_UEXACT8)(nextScbTargQ >> 8));

            if (SCSI_IS_NULL_SCB(nextScbTargQ))
            {
               /* No SCB was linked to the waiting SCB.      */
               /* Make the aborted SCB the last one in the   */
               /* target's execution queue by updating the   */ 
               /* EXETARG_TAIL to point to the aborting SCB. */

               /* Set EXETARG_TAIL to aborting SCB. */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*SCSI_TARGET_UNIT(hiob)->scsiID),
                                  (SCSI_UEXACT8)hiob->scbNumber);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*SCSI_TARGET_UNIT(hiob)->scsiID+1),
                                  (SCSI_UEXACT8)(hiob->scbNumber >> 8));
            }

            nextScbTargQ = 
               (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*SCSI_TARGET_UNIT(hiob)->scsiID)) |
               ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*SCSI_TARGET_UNIT(hiob)->scsiID+1)) << 8);
         }
         else
         {
            /* Enqueue at head of the execution queue */
            SCSIhStandardQHead(hhcb, hiob);
         }

         /* Restore mode 3 SCBPTR. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));

         /* Indicate abort is in progress */
         hiob->SCSI_IP_mgrStat = SCSI_MGR_ABORTINPROG;

      }  /* end of else if not in both Done queues */
   }  /* end of else if SCB is currently running */

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  
}
#endif /* SCSI_STANDARD_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardEnhU320NonPackActiveAbort
*
*     Abort the active HIOB for standard enhanced U320 mode sequencer
*     in non-packetized mode.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 aborted hiob
*
*  Remarks:       Assumes sequencer is paused.
*
*********************************************************************/
#if (SCSI_STANDARD_ENHANCED_U320_MODE && !SCSI_ASPI_SUPPORT)
void SCSIhStandardEnhU320NonPackActiveAbort (SCSI_HHCB SCSI_HPTR hhcb,
                                             SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 scbNumber;
   SCSI_UEXACT16 waitingScb;
   SCSI_UEXACT16 nextScbTargQ;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 byteBuf;
   SCSI_UEXACT8 savedMode;
   SCSI_UEXACT8 m0ccsgctl;
   SCSI_UEXACT8 m0dfcntrl;

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Switch to mode 0 - the mode that uses for data transfer */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE0);

   scbNumber = ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SU320_ACTIVE_SCB+1))) << 8) |
               (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SU320_ACTIVE_SCB)));
   m0ccsgctl = OSD_INEXACT8(SCSI_AICREG(SCSI_CCSGCTL));
   m0dfcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL));

   /* Switch to mode 1 - the mode that uses for data transfer */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE1);

   /* If the aborted HIOB is currently active, then CHIM will   */
   /* have to wait until it is inactive or might be completed.  */
   /* CHIM will set the sequencer break point at the idle loop0 */
   /* again and wait.                                           */
   if (((hiob->scbNumber == scbNumber) &&
        ((m0ccsgctl & SCSI_CCSGEN) || (m0dfcntrl & (SCSI_HDMAEN | SCSI_SCSIEN)))) ||
       ((hiob->scbNumber ==
         (((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SU320_ACTIVE_SCB+1))) << 8) |
           (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SU320_ACTIVE_SCB))))) &&
        ((OSD_INEXACT8(SCSI_AICREG(SCSI_CCSGCTL)) & SCSI_CCSGEN) ||
         (OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL)) & (SCSI_HDMAEN | SCSI_SCSIEN)))))
   {
      /* Set the idle loop0 break point interrupt */
      SCSI_hSETBREAKPOINT(hhcb, (SCSI_UEXACT8)SCSI_ENTRY_IDLE_LOOP0_BREAK);

      /* Set flag to indicate this is a non expander break interrupt */
      hhcb->SCSI_HP_nonExpBreak = 1;
   }
   else
   {
      /* Switch to mode 3 - a known mode for most registers. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

      waitingScb =
         ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
         (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));

      /* If the aborted HIOB is currently a waiting SCB.                */
      /* A waiting SCB is a SCB has been programmed to select a target. */
      if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) &&
          (hiob->scbNumber == waitingScb))
      {
         /* Set aborted status is done. */
         /* The stat and haStat are set before breakpoint interrupt. */
         hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE_ABT;

         /* Prepare scb to be aborted by setting 'aborted' bit in SCB ram */

         /* Save mode 3 SCBPTR value. */
         scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
                  ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                       (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                       (SCSI_UEXACT8)(hiob->scbNumber >> 8));

         /* Set 'aborted' bit of 'scontrol' field in SCB ram. */
         /* Sequencer will check this bit when the selection  */
         /* is done and will send ABORT message to target.    */
         byteBuf = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320, scontrol)));
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,scontrol)),
             byteBuf | SCSI_SEU320_ABORTED);

         /* Restore mode 3 SCBPTR. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));
      }

      /* If the aborted HIOB is currently in the following state:    */
      /*   1. Execution queue - waiting to be executed               */
      /*   2. Disconnection - pending at the target                  */

      /* The aborted HIOB will be posted back if it found in execution queue */
      else if (!SCSI_hSEARCHEXEQ(hhcb, hiob, hiob->haStat, 1))
      {
         /* The aborted HIOB has not been completed, we will issue    */
         /* the abort message to the target.  There is case that a    */
         /* target disconnected and never come back which will result */
         /* in selection time out.  On the other hand, the command    */
         /* will be executed and abort message will be sent to target.*/

         /* Prepare scb to be aborted via special function */

         /* Switch to mode 3 - a known mode for SCBPTR access. */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
         /* Save mode 3 SCBPTR value. */
         scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
                  ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                       (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                       (SCSI_UEXACT8)(hiob->scbNumber >> 8));

         /* Prepare scb to be aborted by setting 'aborted' bit and   */
         /* 'special function' bit of the scontrol flag in SCB ram   */
         /* The 'aborted' bit will be used during reselection.       */
         /* The 'special function' will force the sequencer to start */
         /* this SCB and send out the specified 'special_info' byte. */
         byteBuf = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320, scontrol)));
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,scontrol)),
             (byteBuf | SCSI_SEU320_ABORTED | SCSI_SEU320_SPECFUN));

         /* Setup special opcode - msg_to_targ */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_special_opcode)),
             SCSI_SEU320_MSGTOTARG);

         /* Setup message byte - special_info */
         if ((hiob->SCSI_IF_tagEnable) && (!hiob->SCSI_IF_disallowDisconnect))
         {
            /* Abort tagged command - ABORT TASK message */
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_special_info)),
                SCSI_MSG0D);
         }
         else
         {
            /* Abort non-tagged command - ABORT TASK SET message */
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_special_info)),
                SCSI_MSG06);
         }

         /* If the head SCB in the specified target's execution queue */
         /* is selecting, then we will insert the aborted SCB onto    */
         /* the target's execution queue right next to the selecting  */
         /* SCB.  And re-link the target's execution queue.           */
         if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) &&
             (!SCSI_IS_NULL_SCB(waitingScb)) &&
             (SCSI_ACTPTR[waitingScb] != SCSI_NULL_HIOB) &&
             (SCSI_TARGET_UNIT(SCSI_ACTPTR[waitingScb])->hhcb == hhcb) &&
             (SCSI_TARGET_UNIT(SCSI_ACTPTR[waitingScb])->scsiID == SCSI_TARGET_UNIT(hiob)->scsiID))
         {
            /* Get the next exetarg SCB in the waiting SCB */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                          (SCSI_UEXACT8)waitingScb);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                          (SCSI_UEXACT8)(waitingScb >> 8));

            nextScbTargQ = 
               (SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                  OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320, SCSI_SEU320_q_exetarg_next)))) |
               ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                  OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320, SCSI_SEU320_q_exetarg_next)+1))) << 8);

            /* Insert the aborting SCB in the target's execution */
            /* queue to be next to the waiting SCB.              */
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                  OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                              SCSI_SEU320_q_exetarg_next)),
                               (SCSI_UEXACT8)hiob->scbNumber);
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                  OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                              SCSI_SEU320_q_exetarg_next)+1),
                               (SCSI_UEXACT8)(hiob->scbNumber >> 8));

            /* Link the aborting SCB to the next exetarg SCB */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                          (SCSI_UEXACT8)hiob->scbNumber);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                          (SCSI_UEXACT8)(hiob->scbNumber >> 8));

            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                  OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                              SCSI_SEU320_q_exetarg_next)),
                               (SCSI_UEXACT8)nextScbTargQ);
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                  OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                              SCSI_SEU320_q_exetarg_next)+1),
                               (SCSI_UEXACT8)(nextScbTargQ >> 8));

            if (SCSI_IS_NULL_SCB(nextScbTargQ))
            {
               /* No SCB was linked to the waiting SCB.      */
               /* Make the aborted SCB the last one in the   */
               /* target's execution queue by updating the   */ 
               /* EXETARG_TAIL to point to the aborting SCB. */

               /* Set EXETARG_TAIL to aborting SCB. */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*SCSI_TARGET_UNIT(hiob)->scsiID),
                                  (SCSI_UEXACT8)hiob->scbNumber);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*SCSI_TARGET_UNIT(hiob)->scsiID+1),
                                  (SCSI_UEXACT8)(hiob->scbNumber >> 8));
            }

            nextScbTargQ = 
               (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*SCSI_TARGET_UNIT(hiob)->scsiID)) |
               ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*SCSI_TARGET_UNIT(hiob)->scsiID+1)) << 8);
         }
         else
         {
            /* Enqueue at head of the execution queue */
            SCSIhStandardQHead(hhcb, hiob);
         }

         /* Restore mode 3 SCBPTR. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));

         /* Indicate abort is in progress */
         hiob->SCSI_IP_mgrStat = SCSI_MGR_ABORTINPROG;

      }  /* end of else if not in both Done queues */
   }  /* end of else if SCB is currently running */

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/*********************************************************************
*
*  SCSIhDchU320NonPackActiveAbort
*
*     Abort the active HIOB for standard U320 mode sequencer
*     in non-packetized mode.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 aborted hiob
*
*  Remarks:       Assumes sequencer is paused.
*
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320NonPackActiveAbort (SCSI_HHCB SCSI_HPTR hhcb,
                                     SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 waitingScb;
   SCSI_UEXACT16 nextScbTargQ;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 byteBuf;
   SCSI_UEXACT8 savedMode;

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Switch to mode 1 - the mode that uses for non-packetized data transfer */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE1);

   /* If the aborted HIOB is currently active, then CHIM will   */
   /* have to wait until it is inactive or might be completed.  */
   /* CHIM will set the sequencer break point at the idle loop0 */
   /* again and wait.                                           */
   if ((hiob->scbNumber ==
        (((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_DCHU320_ACTIVE_SCB+1))) << 8) |
          (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_DCHU320_ACTIVE_SCB))))) &&
       ((OSD_INEXACT8(SCSI_AICREG(SCSI_CCSGCTL)) & SCSI_CCSGEN) ||
        (OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL)) & (SCSI_HDMAENACK | SCSI_SCSIENACK))))
   {
      /* Set the idle loop0 break point interrupt */
      SCSI_hSETBREAKPOINT(hhcb, (SCSI_UEXACT8)SCSI_ENTRY_IDLE_LOOP0_BREAK);

      /* Set flag to indicate this is a non expander break interrupt */
      hhcb->SCSI_HP_nonExpBreak = 1;
   }
   else
   {
      /* Switch to mode 3 - a known mode for most registers. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

      waitingScb =
         ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
         (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));

      /* If the aborted HIOB is currently a waiting SCB.                */
      /* A waiting SCB is a SCB has been programmed to select a target. */
      if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) &&
          (hiob->scbNumber == waitingScb))
      {
         /* Set aborted status is done. */
         /* The stat and haStat are set before breakpoint interrupt. */
         hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE_ABT;

         /* Prepare scb to be aborted by setting 'aborted' bit in SCB ram */

         /* Save mode 3 SCBPTR value. */
         scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
                  ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                       (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                       (SCSI_UEXACT8)(hiob->scbNumber >> 8));

         /* Set 'aborted' bit of 'scontrol' field in SCB ram. */
         /* Sequencer will check this bit when the selection  */
         /* is done and will send ABORT message to target.    */
         byteBuf = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                       OSDoffsetof(SCSI_SCB_DCH_U320, scontrol)));
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DCH_U320,scontrol)),
             byteBuf | SCSI_DCHU320_ABORTED);

         /* Restore mode 3 SCBPTR. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));
      }

      /* If the aborted HIOB is currently in the following state:    */
      /*   1. Execution queue - waiting to be executed               */
      /*   2. Disconnection - pending at the target                  */

      /* The aborted HIOB will be posted back if it found in execution queue */
      else if (!SCSIhDchU320SearchExeQ(hhcb, hiob, hiob->haStat, 1))
      {
         /* The aborted HIOB has not been completed, we will issue    */
         /* the abort message to the target.  There is case that a    */
         /* target disconnected and never come back which will result */
         /* in selection time out.  On the other hand, the command    */
         /* will be executed and abort message will be sent to target.*/

         /* Prepare scb to be aborted via special function */

         /* Switch to mode 3 - a known mode for SCBPTR access. */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
         /* Save mode 3 SCBPTR value. */
         scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
                  ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                       (SCSI_UEXACT8)hiob->scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                       (SCSI_UEXACT8)(hiob->scbNumber >> 8));

         /* Prepare scb to be aborted by setting 'aborted' bit and   */
         /* 'special function' bit of the scontrol flag in SCB ram   */
         /* The 'aborted' bit will be used during reselection.       */
         /* The 'special function' will force the sequencer to start */
         /* this SCB and send out the specified 'special_info' byte. */
         byteBuf = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                       OSDoffsetof(SCSI_SCB_DCH_U320, scontrol)));
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DCH_U320,scontrol)),
             (byteBuf | SCSI_DCHU320_ABORTED | SCSI_DCHU320_SPECFUN));

         /* Setup special opcode - msg_to_targ */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_special_opcode)),
             SCSI_DCHU320_MSGTOTARG);

         /* Setup message byte - special_info */
         if ((hiob->SCSI_IF_tagEnable) && (!hiob->SCSI_IF_disallowDisconnect))
         {
            /* Abort tagged command - ABORT TASK message */
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_special_info)),
                SCSI_MSG0D);
         }
         else
         {
            /* Abort non-tagged command - ABORT TASK SET message */
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_special_info)),
                SCSI_MSG06);
         }

         /* If the head SCB in the specified target's execution queue */
         /* is selecting, then we will insert the aborted SCB onto    */
         /* the target's execution queue right next to the selecting  */
         /* SCB.  And re-link the target's execution queue.           */
         if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) &&
             (!SCSI_IS_NULL_SCB(waitingScb)) &&
             (SCSI_ACTPTR[waitingScb] != SCSI_NULL_HIOB) &&
             (SCSI_TARGET_UNIT(SCSI_ACTPTR[waitingScb])->hhcb == hhcb) &&
             (SCSI_TARGET_UNIT(SCSI_ACTPTR[waitingScb])->scsiID == SCSI_TARGET_UNIT(hiob)->scsiID))
         {
            /* Get the next exetarg SCB in the waiting SCB */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                          (SCSI_UEXACT8)waitingScb);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                          (SCSI_UEXACT8)(waitingScb >> 8));

            nextScbTargQ = 
               (SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                  OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)))) |
               ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                  OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)+1))) << 8);

            /* Insert the aborting SCB in the target's execution */
            /* queue to be next to the waiting SCB.              */
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                  OSDoffsetof(SCSI_SCB_DCH_U320,
                                              SCSI_DCHU320_q_exetarg_next)),
                               (SCSI_UEXACT8)hiob->scbNumber);
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                  OSDoffsetof(SCSI_SCB_DCH_U320,
                                              SCSI_DCHU320_q_exetarg_next)+1),
                               (SCSI_UEXACT8)(hiob->scbNumber >> 8));

            /* Link the aborting SCB to the next exetarg SCB */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                          (SCSI_UEXACT8)hiob->scbNumber);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                          (SCSI_UEXACT8)(hiob->scbNumber >> 8));

            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                  OSDoffsetof(SCSI_SCB_DCH_U320,
                                              SCSI_DCHU320_q_exetarg_next)),
                               (SCSI_UEXACT8)nextScbTargQ);
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                  OSDoffsetof(SCSI_SCB_DCH_U320,
                                              SCSI_DCHU320_q_exetarg_next)+1),
                               (SCSI_UEXACT8)(nextScbTargQ >> 8));

            if (SCSI_IS_NULL_SCB(nextScbTargQ))
            {
               /* No SCB was linked to the waiting SCB.      */
               /* Make the aborted SCB the last one in the   */
               /* target's execution queue by updating the   */ 
               /* EXETARG_TAIL to point to the aborting SCB. */

               /* Set EXETARG_TAIL to aborting SCB. */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*SCSI_TARGET_UNIT(hiob)->scsiID),
                                  (SCSI_UEXACT8)hiob->scbNumber);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*SCSI_TARGET_UNIT(hiob)->scsiID+1),
                                  (SCSI_UEXACT8)(hiob->scbNumber >> 8));
            }

            nextScbTargQ = 
               (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*SCSI_TARGET_UNIT(hiob)->scsiID)) |
               ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*SCSI_TARGET_UNIT(hiob)->scsiID+1)) << 8);
         }
         else
         {
            /* Enqueue at head of the execution queue */
            SCSIhDchU320QHead(hhcb, hiob);
         }

         /* Restore mode 3 SCBPTR. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));

         /* Indicate abort is in progress */
         hiob->SCSI_IP_mgrStat = SCSI_MGR_ABORTINPROG;

      }  /* end of else if not in both Done queues */
   }  /* end of else if SCB is currently running */

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  
}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardU320PackActiveAbort
*
*     Abort the active HIOB for standard U320 mode sequencer
*     in packetized mode.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 aborted hiob
*
*  Remarks:       Assumes the sequencer is paused. 
*
*********************************************************************/
#if (SCSI_STANDARD_U320_MODE && SCSI_PACKETIZED_IO_SUPPORT && !SCSI_ASPI_SUPPORT)
void SCSIhStandardU320PackActiveAbort (SCSI_HHCB SCSI_HPTR hhcb,
                                       SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT32 i;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 byteBuf;
   SCSI_UEXACT8 savedMode;

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Switch to mode 3 - a known mode for most registers. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* If hardware is programmed to select or currently selecting the target */
   /* or sequencer has not been processed the selection done and the target */
   /* ID matches the aborted HIOB's target ID, then CHIM will have to wait  */
   /* until these conditions are clear.  CHIM will set the break point at   */
   /* the idle loop0 again and wait.                                        */
   if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SELOID)) == SCSI_TARGET_UNIT(hiob)->scsiID) &&
       ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) ||
        (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & SCSI_SELDO)))
   {
      /* IMPORTANT NOTE:                                                */
      /* THE BELOW WORKAROUND APPLIES TO HARPOON2 REV A ONLY.  IF THIS  */
      /* ROUTINE EVER COMBINES WITH STANDARD ENHANCED U320, THEN WE     */
      /* SHOULD HAVE A RUN-TIME CHECK FOR STANDARD U320 FIRMWARE MODE.  */
      /* AND IT MIGHT BE BETTER TO HAVE A CONDITIONAL COMPILE OPTION,   */
      /* SCSI_STANDARD_U320_MODE, TO SAVE CODE SPACE.                   */

      /* Harpoon2 A4 chips has a problem where we cannot set the same   */
      /* break point address after servicing it.  The system will hang  */
      /* after sequencer is unpaused.  The details issue can be found   */
      /* in the Razor database entry #662 (Harpoon 2 A4).  It is changed*/
      /* to #743 in Harpoon 2 Rev B.                                    */

      /* At this implementation, CHIM will single step the sequencer    */
      /* code inorder to setup the same break point address at          */
      /* sequencer idle loop0.                                          */
      /* Sequencer has been added a 'nop' instruction at the idle_loop0 */
      /* label for proper implementation below.  Because the instruction*/
      /* at the idle_loop0 label might change the mode pointer or       */
      /* something else.                                                */

      /* H2A4 WORKAROUND START */

      /* Disable scsi-to-sequencer interrupt */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQINTCTL),
          (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_SEQINTCTL)) | SCSI_INTVEC1DSL));

      /* Clear break address interrupt */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRBRKADRINT);

      /* Setup to Single Step sequencer */
      byteBuf = (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_SEQCTL0)) & ~SCSI_BRKADRINTEN);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0), (byteBuf | SCSI_STEP));

      /* Unpause sequencer to execute current instruction */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_HCNTRL),
          (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & ~SCSI_PAUSE));

      /* Make sure the chip is paused before proceed */
      i = 0;
      while ((!(OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & SCSI_PAUSEACK)) &&
             (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
      {
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }    

      /* Clear Single Step sequencer */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0), byteBuf);

      /* Enable scsi-to-sequencer interrupt */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQINTCTL),
          (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_SEQINTCTL)) & ~SCSI_INTVEC1DSL));

      /* H2A4 WORKAROUND END */

      /* Set the idle loop0 break point interrupt */
      SCSI_hSETBREAKPOINT(hhcb, (SCSI_UEXACT8)SCSI_ENTRY_IDLE_LOOP0_BREAK);

      /* Set flag to indicate this is a non expander break interrupt */
      hhcb->SCSI_HP_nonExpBreak = 1;
   }

   /* Check if the aborted HIOB is currently at:    */
   /*   1. Execution queue - waiting to be executed */
   /*   2. Disconnection - pending at target        */

   /* The aborted HIOB will be posted back if it found in execution queue */
   else if (!SCSI_hSEARCHEXEQ(hhcb, hiob, hiob->haStat, 1))
   {
      /* The aborting HIOB has not been completed, we will setup SCB   */
      /* to issue the task management flag (ABORT TASK) to the target. */
      /* There is a case that a target disconnected and never come back*/
      /* which will result in selection time out.  On the other hand,  */
      /* the SCB will be executed and specified task management flag   */
      /* will be sent to target.                                       */

      /* Prepare scb to be aborted via task management flag */

      /* Switch to mode 3 - a known mode for SCBPTR access. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
      /* Save mode 3 SCBPTR value. */
      scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
               ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      /* Setup 'aborted' bit and 'special function' bit of the scontrol */
      /* flag in SCB ram.                                               */
      /* 1. Sequencer uses aborted bit during selection done to put it  */
      /*    into done queue.                                            */
      /* 2. LQIMGR uses aborted bit to check for the aborted SCB on     */
      /*    incoming LQ.                                                */
      /* 3. Sequencer uses 'special function' bit to start the command  */
      byteBuf = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                    OSDoffsetof(SCSI_SCB_STANDARD_U320, scontrol)));
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_STANDARD_U320,scontrol)),
          (byteBuf | SCSI_SU320_ABORTED | SCSI_SU320_SPECFUN));

      /* Setup task attribute - simple task attribute */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_STANDARD_U320,task_attribute)),
          SCSI_SIMPLE_TASK_ATTRIBUTE);

      /* Setup task management flag - ABORT TASK */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_STANDARD_U320,task_management)),
          SCSI_ABORT_TASK);

      /* Enqueue at head of the execution queue */
      SCSIhStandardQHead(hhcb, hiob);

      /* Restore mode 3 SCBPTR. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(scbptr >> 8));

/* REVISIT - The code will be tested when hardware fix ABORTPENDING issue */
#if 0
      /* Currently the feature is off because hardware does not work */

      /* REVISIT - Need to clear this bit when all aborted HIOB are done. */
      /* Setup hardware, LQIMGR, to check for every incoming LQ to see */
      /* if it corresponding to an aborted command.                    */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQCTL1), SCSI_ABORTPENDING);
#endif /* 0 */

      /* Indicate abort is done */
      hiob->SCSI_IP_mgrStat = SCSI_MGR_ABORTED;
   }  /* end of if selection or selection done */

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  
}
#endif /* (SCSI_STANDARD_U320_MODE && SCSI_PACKETIZED_IO_SUPPORT) */

/*********************************************************************
*
*  SCSIhStandardEnhU320PackActiveAbort
*
*     Abort the active HIOB for standard enhanced U320 mode sequencer
*     in packetized mode.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 aborted hiob
*
*  Remarks:       Assumes the sequencer is paused. 
*
*********************************************************************/
#if (SCSI_STANDARD_ENHANCED_U320_MODE && SCSI_PACKETIZED_IO_SUPPORT && !SCSI_ASPI_SUPPORT)
void SCSIhStandardEnhU320PackActiveAbort (SCSI_HHCB SCSI_HPTR hhcb,
                                          SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 byteBuf;
   SCSI_UEXACT8 savedMode;

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Switch to mode 3 - a known mode for most registers. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* If hardware is programmed to select or currently selecting the target */
   /* or sequencer has not been processed the selection done and the target */
   /* ID matches the aborted HIOB's target ID, then CHIM will have to wait  */
   /* until these conditions are clear.  CHIM will set the break point at   */
   /* the idle loop0 again and wait.                                        */
   if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SELOID)) == SCSI_TARGET_UNIT(hiob)->scsiID) &&
       ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) ||
        (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & SCSI_SELDO)))
   {
      /* Set the idle loop0 break point interrupt */
      SCSI_hSETBREAKPOINT(hhcb, (SCSI_UEXACT8)SCSI_ENTRY_IDLE_LOOP0_BREAK);

      /* Set flag to indicate this is a non expander break interrupt */
      hhcb->SCSI_HP_nonExpBreak = 1;
   }

   /* Check if the aborted HIOB is currently at:    */
   /*   1. Execution queue - waiting to be executed */
   /*   2. Disconnection - pending at target        */

   /* The aborted HIOB will be posted back if it found in execution queue */
   else if (!SCSI_hSEARCHEXEQ(hhcb, hiob, hiob->haStat, 1))
   {
      /* The aborting HIOB has not been completed, we will setup SCB   */
      /* to issue the task management flag (ABORT TASK) to the target. */
      /* There is a case that a target disconnected and never come back*/
      /* which will result in selection time out.  On the other hand,  */
      /* the SCB will be executed and specified task management flag   */
      /* will be sent to target.                                       */

      /* Prepare scb to be aborted via task management flag */

      /* Switch to mode 3 - a known mode for SCBPTR access. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
      /* Save mode 3 SCBPTR value. */
      scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
               ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      /* Setup 'aborted' bit and 'special function' bit of the scontrol */
      /* flag in SCB ram.                                               */
      /* 1. Sequencer uses aborted bit during selection done to put it  */
      /*    into done queue.                                            */
      /* 2. LQIMGR uses aborted bit to check for the aborted SCB on     */
      /*    incoming LQ.                                                */
      /* 3. Sequencer uses 'special function' bit to start the command  */
      byteBuf = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                    OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320, scontrol)));
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,scontrol)),
          (byteBuf | SCSI_SEU320_ABORTED | SCSI_SEU320_SPECFUN));

      /* Setup task attribute - simple task attribute */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,task_attribute)),
          SCSI_SIMPLE_TASK_ATTRIBUTE);

      /* Setup task management flag - ABORT TASK */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,task_management)),
          SCSI_ABORT_TASK);

      /* Enqueue at head of the execution queue */
      SCSIhStandardQHead(hhcb, hiob);

      /* Restore mode 3 SCBPTR. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(scbptr >> 8));

/* REVISIT - The code will be tested when hardware fix ABORTPENDING issue */
#if 0
      /* Currently the feature is off because hardware does not work */

      /* REVISIT - Need to clear this bit when all aborted HIOB are done. */
      /* Setup hardware, LQIMGR, to check for every incoming LQ to see */
      /* if it corresponding to an aborted command.                    */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQCTL1), SCSI_ABORTPENDING);
#endif /* 0 */

      /* Indicate abort is done */
      hiob->SCSI_IP_mgrStat = SCSI_MGR_ABORTED;
   }  /* end of if selection or selection done */

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  
}
#endif /* (SCSI_STANDARD_ENHANCED_U320_MODE && SCSI_PACKETIZED_IO_SUPPORT) */

/*********************************************************************
*
*  SCSIhDchU320PackActiveAbort
*
*     Abort the active HIOB for standard U320 mode sequencer
*     in packetized mode.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 aborted hiob
*
*  Remarks:       Assumes the sequencer is paused. 
*
*********************************************************************/
#if (SCSI_DCH_U320_MODE && SCSI_PACKETIZED_IO_SUPPORT)
void SCSIhDchU320PackActiveAbort (SCSI_HHCB SCSI_HPTR hhcb,
                                  SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 byteBuf;
   SCSI_UEXACT8 savedMode;

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Switch to mode 3 - a known mode for most registers. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* If hardware is programmed to select or currently selecting the target */
   /* or sequencer has not been processed the selection done and the target */
   /* ID matches the aborted HIOB's target ID, then CHIM will have to wait  */
   /* until these conditions are clear.  CHIM will set the break point at   */
   /* the idle loop0 again and wait.                                        */
   if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SELOID)) == SCSI_TARGET_UNIT(hiob)->scsiID) &&
       ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) ||
        (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & SCSI_SELDO)))
   {
      /* Set the idle loop0 break point interrupt */
      SCSI_hSETBREAKPOINT(hhcb, (SCSI_UEXACT8)SCSI_ENTRY_IDLE_LOOP0_BREAK);

      /* Set flag to indicate this is a non expander break interrupt */
      hhcb->SCSI_HP_nonExpBreak = 1;
   }

   /* Check if the aborted HIOB is currently at:    */
   /*   1. Execution queue - waiting to be executed */
   /*   2. Disconnection - pending at target        */

   /* The aborted HIOB will be posted back if it found in execution queue */
   else if (!SCSIhDchU320SearchExeQ(hhcb, hiob, hiob->haStat, 1))
   {
      /* The aborting HIOB has not been completed, we will setup SCB   */
      /* to issue the task management flag (ABORT TASK) to the target. */
      /* There is a case that a target disconnected and never come back*/
      /* which will result in selection time out.  On the other hand,  */
      /* the SCB will be executed and specified task management flag   */
      /* will be sent to target.                                       */

      /* Prepare scb to be aborted via task management flag */

      /* Switch to mode 3 - a known mode for SCBPTR access. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
      /* Save mode 3 SCBPTR value. */
      scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
               ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      /* Setup 'aborted' bit and 'special function' bit of the scontrol */
      /* flag in SCB ram.                                               */
      /* 1. Sequencer uses aborted bit during selection done to put it  */
      /*    into done queue.                                            */
      /* 2. LQIMGR uses aborted bit to check for the aborted SCB on     */
      /*    incoming LQ.                                                */
      /* 3. Sequencer uses 'special function' bit to start the command  */
      byteBuf = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                    OSDoffsetof(SCSI_SCB_DCH_U320, scontrol)));
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DCH_U320,scontrol)),
          (byteBuf | SCSI_DCHU320_ABORTED | SCSI_DCHU320_SPECFUN));

      /* Setup task attribute - simple task attribute */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DCH_U320,task_attribute)),
          SCSI_SIMPLE_TASK_ATTRIBUTE);

      /* Setup task management flag - ABORT TASK */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DCH_U320,task_management)),
          SCSI_ABORT_TASK);

      /* Enqueue at head of the execution queue */
      SCSIhDchU320QHead(hhcb, hiob);

      /* Restore mode 3 SCBPTR. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(scbptr >> 8));

/* REVISIT - The code will be tested when hardware fix ABORTPENDING issue */
#if 0
      /* Currently the feature is off because hardware does not work */

      /* REVISIT - Need to clear this bit when all aborted HIOB are done. */
      /* Setup hardware, LQIMGR, to check for every incoming LQ to see */
      /* if it corresponding to an aborted command.                    */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQCTL1), SCSI_ABORTPENDING);
#endif /* 0 */

      /* Indicate abort is done */
      hiob->SCSI_IP_mgrStat = SCSI_MGR_ABORTED;
   }  /* end of if selection or selection done */

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  
}
#endif /* (SCSI_DCH_U320_MODE && SCSI_PACKETIZED_IO_SUPPORT) */
/*********************************************************************
*
*  SCSIhStandardRemoveActiveAbort
*
*     Remove active abort from execution queue or from the state of
*     selecting the target.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       The function is called only for non-packetized mode.
*
*********************************************************************/
#if ((SCSI_STANDARD_MODE) && !(SCSI_ASPI_SUPPORT))
void SCSIhStandardRemoveActiveAbort (SCSI_HHCB SCSI_HPTR hhcb,
                                     SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 scsiseq;
   SCSI_UEXACT8 savedMode;
   SCSI_UEXACT16 activeScb;

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* As this function will be called in non-packetized mode only.     */
   /* Sequencer must interrupt CHIM to clean up an active aborted HIOB */
   /* during reselection and must have allocated the free data path.   */
   /* So, we must switch to original mode to get active_SCB.           */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);
   activeScb =
      ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb)+1))) << 8) |
       (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb))));

   /* Switch to mode 3 - a known mode for most registers. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   scsiseq = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0));

   /* If the active aborted SCB is curretly selecting the target */
   if ((scsiseq & SCSI_ENSELO) &&
       (activeScb ==
        (((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
          (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB))))))
   {
      hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE;

      /* Disable any outstanding selection */
      /* Reselection occurred while the active abort SCB is selecting. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ0),
          scsiseq & ~(SCSI_TEMODEO+SCSI_ENSELO));
      scsiseq = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)); /* To flush the cache */

      /* Mend the execution queue with the current target execution queue */
      SCSI_hUPDATEEXEQ(hhcb,hiob);
   }
   
   /* Remove the active abort SCB if found in execution queue */
   else if (SCSI_hSEARCHEXEQ(hhcb, hiob, hiob->haStat, 0))
   {
      hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE;
   }

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  
}
#endif /* SCSI_STANDARD_MODE */

/*********************************************************************
*
*  SCSIhStandardU320ResidueCalc
*
*     Calculate residual length.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Incoming Mode is either 0 or 1
*                  
*********************************************************************/
#if SCSI_STANDARD_U320_MODE
SCSI_UEXACT32 SCSIhStandardU320ResidueCalc (SCSI_HHCB SCSI_HPTR hhcb,
                                            SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;
   void SCSI_LPTR virtualAddress;
   SCSI_BUS_ADDRESS sgPointerSCB;
   SCSI_BUFFER_DESCRIPTOR SCSI_LPTR sgList;
   SCSI_UEXACT32 sgLen;
   SCSI_UEXACT8 currentSGListSize = sizeof(SCSI_BUS_ADDRESS);
   SCSI_UEXACT8 sgCacheWork = 0;
   SCSI_UEXACT32 residueWork = 0;
   SCSI_UINT32 count;          /* time out counter */
   int i = 0;
   int currentSGindex;  
     
   /* Return if no data */
   if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,sg_cache_SCB))) & SCSI_SU320_NODATA)
   {
      return(0);
   }

#if (OSD_BUS_ADDRESS_SIZE == 64)
   if (hhcb->SCSI_HF_SgBusAddress32)
   {
      currentSGListSize = 4;
   }
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */

   /* Put embedded s/g element length into residueWork. */
   /* If CDB length in SCSI_SU320_CDBLENMSK is >0, transfer was not started.  */
   /*    Need to initialize residueWork with embedded s/g element,            */
   /*    Current s/g pointer is from sg_pointer_SCB                           */
   /* Else                                                                    */
   /*    SCSI_SU320_sdata_residue0-3 are used to intitialize residueWork      */
   /*    Current s/g pointer is from sg_pointer_work                          */
   if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,scdb_length))) & SCSI_SU320_CDBLENMSK)
   {
      /* Get current segment length                                           */
      /*    Its ok to use SCSIhGetAddressBA32 cause we know length is 3 bytes */
      /*    regardless of Bus Address size                                    */
      residueWork = (SCSI_UEXACT32)((SCSI_UEXACT32)SCSIhGetEntity32FromScb(hhcb,
                    (SCSI_UEXACT16)(SCSI_SCB_BASE+
                    OSDoffsetof(SCSI_SCB_STANDARD_U320,slength0))) & 0x00FFFFFF);

      /* Get s/g pointer from SCB - this is pointer to s/g cache line.     */
      /* Actual s/g element pointer is index (s/g cache work) + s/g cache  */
      /* line pointer.                                                     */
#if SCSI_ASPI_SUPPORT

      SCSI_hGETADDRESSSCB(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_sg_pointer_SCB0)));
      sgPointerSCB = hhcb->busAddress;
#else

      sgPointerSCB = SCSI_hGETADDRESSSCB(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_sg_pointer_SCB0)));

#endif /* SCSI_ASPI_SUPPORT */
      sgCacheWork = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_U320,sg_cache_SCB)));

      if (sgCacheWork & SCSI_SU320_ONESGSEG) 
      {
         return(residueWork);
      }
   }
   else
   {
      /* Get current segment length                                           */
      /* Its ok to use SCSIhGetEntity32FromScb cause we know sdata_residue is */
      /*    4 bytes regardless of Bus Address size                            */
      residueWork = (SCSI_UEXACT32)(SCSIhGetEntity32FromScb(hhcb,
                    (SCSI_UEXACT16)(SCSI_SCB_BASE+
                    OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_sdata_residue0)))
                    & 0x00FFFFFF);

      /* Get s/g pointer from SCB - this is pointer to s/g cache line.     */
      /* Actual s/g element pointer is index (s/g cache work) + s/g cache  */
      /* line pointer.                                                     */
#if OSD_BUS_ADDRESS_SIZE == 64
      ((SCSI_OCTLET SCSI_IPTR)&sgPointerSCB)->u32.order0 = 
         (SCSI_UEXACT32) SCSIhGetEntity32FromScb(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_sg_pointer_work0)));
      ((SCSI_OCTLET SCSI_IPTR)&sgPointerSCB)->u32.order1 = 
         (SCSI_UEXACT32) SCSIhGetEntity32FromScb(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_sg_pointer_SCB4)));
#else /* !OSD_BUS_ADDRESS == 64 */
#if SCSI_ASPI_SUPPORT

      SCSIhGetAddressScbBA32(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_sg_pointer_work0)));
      sgPointerSCB = hhcb->busAddress;
#else

      sgPointerSCB = SCSIhGetAddressScbBA32(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_sg_pointer_work0)));

#endif /* SCSI_ASPI_SUPPORT */
#endif /* OSD_BUS_ADDRESS_SIZE == 64 */
   
      /* Get s/g cache work */
      sgCacheWork = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_sg_cache_work)));

      if (sgCacheWork & SCSI_SU320_ONESGSEG)
      {
         return(residueWork);
      }
   }
   
   /* Get the S/G List (its a buffer descriptor) */
   sgList = OSD_GET_SG_LIST(hiob);
   busAddress = sgList->busAddress;
   virtualAddress = sgList->virtualAddress;

   /* Must walk down s/g list until at the same point sequencer */
   /* or time out counter expires.                              */                     
   count = (SCSI_UINT32)0x800000;
   while (1)
   {
      if ((!SCSI_hCOMPAREBUSSGLISTADDRESS(hhcb,sgPointerSCB,busAddress)) || (--count == 0))
      {
         break;
      }
      else 
      {
         OSD_ADJUST_BUS_ADDRESS(hhcb,&busAddress,2*currentSGListSize);
         i++;
      }
   }

   /* S/G element is i + index into s/g cache line pointer */
   i += sgCacheWork/((SCSI_UEXACT8)(2*currentSGListSize));

   /* sgPointerSCB point to next sg element, need to adjust it to get */
   /* current index. i should always not be equal to 0, though. */
   if (i == 0)
   {
      currentSGindex = i;
   }
   else
   {
      currentSGindex = i-1;
   }

   /* If current s/g element is last one then don't walk down list */
   SCSI_GET_LITTLE_ENDIAN32(hhcb,&sgLen,virtualAddress,
        (currentSGindex*2*currentSGListSize)+currentSGListSize);
   if  (sgLen & (SCSI_UEXACT32)0x80000000)
   {
      return(residueWork);
   }

   /* Walk down remainder of s/g list and sum the length portions of the s/g  */
   /* elements. When the delimiter is encountered, or timeout counter expires */
   /* then exit. */
   count=(SCSI_UINT32)0x800000;    
   while (1)
   {
      SCSI_GET_LITTLE_ENDIAN32(hhcb,&sgLen,virtualAddress,
           (i*2*currentSGListSize)+currentSGListSize);

      residueWork += (sgLen & 0x00FFFFFF);
      /* to handle one segment and zero in length passed in */
      if ((sgLen & (SCSI_UEXACT32)0x80000000) || (--count == 0))
      {
         break;
      }
      i++;
   }
   return(residueWork);
}
#endif /* SCSI_STANDARD_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardEnhU320ResidueCalc
*
*     Calculate residual length for standard enhanced sequencer.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Incoming Mode is either 0 or 1
*                  
*********************************************************************/
#if SCSI_STANDARD_ENHANCED_U320_MODE
SCSI_UEXACT32 SCSIhStandardEnhU320ResidueCalc (SCSI_HHCB SCSI_HPTR hhcb,
                                               SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;
   void SCSI_LPTR virtualAddress;
   SCSI_BUS_ADDRESS sgPointerSCB;
   SCSI_BUFFER_DESCRIPTOR SCSI_LPTR sgList;
   SCSI_UEXACT32 sgLen;
   SCSI_UEXACT8 currentSGListSize = sizeof(SCSI_BUS_ADDRESS);
   SCSI_UEXACT8 sgCacheWork = 0;
   SCSI_UEXACT32 residueWork = 0;
   SCSI_UINT32 count;          /* time out counter */
   int i = 0;
   int currentSGindex;  
     
   /* Return if no data */
   if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,sg_cache_SCB))) & SCSI_SEU320_NODATA)
   {
      return(0);
   }

#if (OSD_BUS_ADDRESS_SIZE == 64)
   if (hhcb->SCSI_HF_SgBusAddress32)
   {
      currentSGListSize = 4;
   }
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */

   /* Put embedded s/g element length into residueWork. */
   /* If CDB length in SCSI_SU320_CDBLENMSK is >0, transfer was not started.  */
   /*    Need to initialize residueWork with embedded s/g element,            */
   /*    Current s/g pointer is from sg_pointer_SCB                           */
   /* Else                                                                    */
   /*    SCSI_SU320_sdata_residue0-3 are used to intitialize residueWork      */
   /*    Current s/g pointer is from sg_pointer_work                          */
   if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,scdb_length))) & SCSI_SEU320_CDBLENMSK)
   {
      /* Get current segment length                                           */
      /*    Its ok to use SCSIhGetAddressBA32 cause we know length is 3 bytes */
      /*    regardless of Bus Address size                                    */
      residueWork = (SCSI_UEXACT32)((SCSI_UEXACT32)SCSIhGetEntity32FromScb(hhcb,
                    (SCSI_UEXACT16)(SCSI_SCB_BASE+
                    OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,slength0))) & 0x00FFFFFF);

      /* Get s/g pointer from SCB - this is pointer to s/g cache line.     */
      /* Actual s/g element pointer is index (s/g cache work) + s/g cache  */
      /* line pointer.                                                     */
#if SCSI_ASPI_SUPPORT

      SCSI_hGETADDRESSSCB(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_sg_pointer_SCB0)));
      sgPointerSCB = hhcb->busAddress;
#else

      sgPointerSCB = SCSI_hGETADDRESSSCB(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_sg_pointer_SCB0)));

#endif /* SCSI_ASPI_SUPPORT */
      sgCacheWork = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,sg_cache_SCB)));

      if (sgCacheWork & SCSI_SEU320_ONESGSEG) 
      {
         return(residueWork);
      }
   }
   else
   {
      /* Get current segment length                                           */
      /* Its ok to use SCSIhGetEntity32FromScb cause we know sdata_residue is */
      /*    4 bytes regardless of Bus Address size                            */
      residueWork = (SCSI_UEXACT32)(SCSIhGetEntity32FromScb(hhcb,
                    (SCSI_UEXACT16)(SCSI_SCB_BASE+
                    OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_sdata_residue0)))
                    & 0x00FFFFFF);

      /* Get s/g pointer from SCB - this is pointer to s/g cache line.     */
      /* Actual s/g element pointer is index (s/g cache work) + s/g cache  */
      /* line pointer.                                                     */
#if OSD_BUS_ADDRESS_SIZE == 64
      ((SCSI_OCTLET SCSI_IPTR)&sgPointerSCB)->u32.order0 = 
         (SCSI_UEXACT32) SCSIhGetEntity32FromScb(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_sg_pointer_work0)));
      ((SCSI_OCTLET SCSI_IPTR)&sgPointerSCB)->u32.order1 = 
         (SCSI_UEXACT32) SCSIhGetEntity32FromScb(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_sg_pointer_SCB4)));
#else /* !OSD_BUS_ADDRESS == 64 */
#if SCSI_ASPI_SUPPORT

      SCSIhGetAddressScbBA32(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_sg_pointer_work0)));
      sgPointerSCB = hhcb->busAddress;
#else

      sgPointerSCB = SCSIhGetAddressScbBA32(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_sg_pointer_work0)));

#endif /* SCSI_ASPI_SUPPORT */
#endif /* OSD_BUS_ADDRESS_SIZE == 64 */
   
      /* Get s/g cache work */
      sgCacheWork = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_sg_cache_work)));

      if (sgCacheWork & SCSI_SEU320_ONESGSEG)
      {
         return(residueWork);
      }
   }
   
   /* Get the S/G List (its a buffer descriptor) */
   sgList = OSD_GET_SG_LIST(hiob);
   busAddress = sgList->busAddress;
   virtualAddress = sgList->virtualAddress;

   /* Must walk down s/g list until at the same point sequencer */
   /* or time out counter expires.                              */                     
   count = (SCSI_UINT32)0x800000;
   while (1)
   {
      if ((!SCSI_hCOMPAREBUSSGLISTADDRESS(hhcb,sgPointerSCB,busAddress)) || (--count == 0))
      {
         break;
      }
      else 
      {
         OSD_ADJUST_BUS_ADDRESS(hhcb,&busAddress,2*currentSGListSize);
         i++;
      }
   }

   /* S/G element is i + index into s/g cache line pointer */
   i += sgCacheWork/((SCSI_UEXACT8)(2*currentSGListSize));

   /* sgPointerSCB point to next sg element, need to adjust it to get */
   /* current index. i should always not be equal to 0, though. */
   if (i == 0)
   {
      currentSGindex = i;
   }
   else
   {
      currentSGindex = i-1;
   }

   /* If current s/g element is last one then don't walk down list */
   SCSI_GET_LITTLE_ENDIAN32(hhcb,&sgLen,virtualAddress,
        (currentSGindex*2*currentSGListSize)+currentSGListSize);
   if  (sgLen & (SCSI_UEXACT32)0x80000000)
   {
      return(residueWork);
   }

   /* Walk down remainder of s/g list and sum the length portions of the s/g  */
   /* elements. When the delimiter is encountered, or timeout counter expires */
   /* then exit. */
   count=(SCSI_UINT32)0x800000;    
   while (1)
   {
      SCSI_GET_LITTLE_ENDIAN32(hhcb,&sgLen,virtualAddress,
           (i*2*currentSGListSize)+currentSGListSize);

      residueWork += (sgLen & 0x00FFFFFF);
      /* to handle one segment and zero in length passed in */
      if ((sgLen & (SCSI_UEXACT32)0x80000000) || (--count == 0))
      {
         break;
      }
      i++;
   }
   return(residueWork);
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/*********************************************************************
*
*  SCSIhDchU320ResidueCalc
*
*     Calculate residual length.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:                
*                 MUST BE MODIFIED FOR DCH-SCSI-- Generic SG support        
*                  
*********************************************************************/
#if SCSI_DCH_U320_MODE
SCSI_UEXACT32 SCSIhDchU320ResidueCalc (SCSI_HHCB SCSI_HPTR hhcb,
                                            SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;
   void SCSI_LPTR virtualAddress;
   SCSI_BUS_ADDRESS sgPointerSCB;
   SCSI_UEXACT32 sgLen, sgLastElement;
   SCSI_BUFFER_DESCRIPTOR SCSI_LPTR sgList;
   SCSI_UEXACT16 sgCacheWork = 0;
   SCSI_UEXACT32 residueWork = 0;
   SCSI_UINT32 count;          /* time out counter */
   int i = 0;
   int currentSGindex;  

#if (OSD_BUS_ADDRESS_SIZE == 64)
   SCSI_UEXACT8 currentSGListSize = sizeof(SCSI_BUS_ADDRESS);
#else
   SCSI_UEXACT8 currentSGListSize = (sizeof(SCSI_BUS_ADDRESS) * 2);
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */

     
   /* Return if no data */
   if (OSD_INEXACT16_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,sg_cache_SCB))) & SCSI_DCHU320_NODATA)
   {
      return(0);
   }

   /* Put embedded s/g element length into residueWork. */
   /* If CDB length in SCSI_DCHU320_CDBLENMSK is >0, transfer was not started.  */
   /*    Need to initialize residueWork with embedded s/g element,            */
   /*    Current s/g pointer is from sg_pointer_SCB                           */
   /* Else                                                                    */
   /*    SCSI_DCHU320_sdata_residue0-3 are used to intitialize residueWork      */
   /*    Current s/g pointer is from sg_pointer_work                          */
   
   if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,scdb_length))) & SCSI_DCHU320_CDBLENMSK)
   {
      /* Get current segment length                                           */
      residueWork = (SCSI_UEXACT32)(SCSIhGetEntity32FromScb(hhcb,
                    (SCSI_UEXACT16)(SCSI_SCB_BASE+
                    OSDoffsetof(SCSI_SCB_DCH_U320,slength0))));

      /* Get s/g pointer from SCB - this is pointer to s/g cache line.     */
      /* Actual s/g element pointer is index (s/g cache work) + s/g cache  */
      /* line pointer.                                                     */
      sgPointerSCB = SCSI_hGETADDRESSSCB(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_sg_pointer_SCB0)));
                     
      sgCacheWork = OSD_INEXACT16_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DCH_U320,sg_cache_SCB)));
         
      if (sgCacheWork & SCSI_DCHU320_ONESGSEG) 
      {
         return(residueWork);
      }
   }
   else
   {
      /* Get current segment length                                           */
      /* Its ok to use SCSIhGetEntity32FromScb cause we know sdata_residue is */
      /*    4 bytes regardless of Bus Address size                            */

      residueWork = (SCSI_UEXACT32)(SCSIhGetEntity32FromScb(hhcb,
                    (SCSI_UEXACT16)(SCSI_SCB_BASE+
                    OSDoffsetof(SCSI_SCB_DCH_U320,slength0))));
      
      /* Get s/g pointer from SCB - this is pointer to s/g cache line.     */
      /* Actual s/g element pointer is index (s/g cache work) + s/g cache  */
      /* line pointer.                                                     */
      
      sgPointerSCB = SCSI_hGETADDRESSSCB(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_sg_pointer_SCB0)));

      /* Get s/g cache work */
      sgCacheWork = OSD_INEXACT16_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DCH_U320,sg_cache_SCB)));
         
      if (sgCacheWork & SCSI_DCHU320_ONESGSEG)
      {
         return(residueWork);
      }
   }
   
   /* Get the S/G List (its a buffer descriptor) */

   sgList = OSD_GET_SG_LIST(hiob);
   busAddress = sgList->busAddress;
   virtualAddress = sgList->virtualAddress;

   /* 
      Here we have a distinct difference with SUB-List support. Finding if a LE bit 
      exist and then getting the next SUB-List address is the trick. 
   */
   count=(SCSI_UINT32)0x800000;

   while (1)
   {
      if ((!SCSI_hCOMPAREBUSSGLISTADDRESS(hhcb,sgPointerSCB,busAddress)) || (--count == 0))
      {
         break;
      }
      else
      {
         SCSI_GET_LITTLE_ENDIAN32(hhcb,&sgLastElement,virtualAddress,
           (i*2*currentSGListSize)+(currentSGListSize+4));
         /* are we at the end of the list? */
         if ((sgLastElement & (SCSI_UEXACT32)0x40000000) || (--count == 0))
         {
            break;
         }

         /* are we at the last element in this list ? */
         else if ((sgLastElement & (SCSI_UEXACT32)0x80000000) || (--count == 0))
         {
            /* get the pointer to the next sublist */
            SCSI_GET_LITTLE_ENDIAN32(hhcb,&busAddress,virtualAddress,
               (i*2*currentSGListSize)+(currentSGListSize+8));

            /****************************************************************** 
               The link Address located in the SG record preceding the LE for 
               the current embedded us of a DCH does not required a translation 
               from a virtualAddress to physical as all memory used for SGLs 
               resides with in the IOPs memory soace.
             ******************************************************************/

            virtualAddress = busAddress;
            /* reset offset for next SG list */
            i=0;
         }

         /* no just grab the next element and keep searching for the stop address */
         else
         {
            OSD_ADJUST_BUS_ADDRESS(hhcb,&busAddress,2*currentSGListSize);
            i++;
         }
      }
   }

   currentSGindex = i = (sgCacheWork>>8)/((SCSI_UEXACT8)(2*currentSGListSize));

   /* sgPointerSCB point to next sg element, need to adjust it to get */
   
   /* Changes here for $$ DCH $$ */   

   /* Walk down remainder of s/g list and sum the length portions of the s/g  */
   /* elements. When the delimiter is encountered, or timeout counter expires */
   /* then exit. */
   count=(SCSI_UINT32)0x800000;    
   while (1)
   {

      SCSI_GET_LITTLE_ENDIAN32(hhcb,&sgLen,virtualAddress,
           (i*2*currentSGListSize)+currentSGListSize);
      
      /* grab the 32 bit attribute Dword */         
      SCSI_GET_LITTLE_ENDIAN32(hhcb,&sgLastElement,virtualAddress,
           (i*2*currentSGListSize)+(currentSGListSize+4));

      /* are we at the last element in this list ? */
      if ((sgLastElement & (SCSI_UEXACT32)0x80000000) || (--count == 0))
      {
         /* get the pointer to the next sublist */
         SCSI_GET_LITTLE_ENDIAN32(hhcb,&busAddress,virtualAddress,
            (i*2*currentSGListSize)+(currentSGListSize+8));

         /****************************************************************** 
            The link Address located in the SG record preceding the LE for 
            the current embedded us of a DCH does not required a translation 
            from a virtualAddress to physical as all memory used for SGLs 
            resides with in the IOPs memory soace.
          ******************************************************************/

         virtualAddress = busAddress;
         /* reset offset for next SG list */
         i=0;
      }

      /* no just grab the next element and keep searching for the LL flag */
      else
      {
         OSD_ADJUST_BUS_ADDRESS(hhcb,&busAddress,2*currentSGListSize);
         i++;
      }

      residueWork += sgLen;   /* no masking needed all 32 bits can be used */

      /* to handle one segment and zero in length passed in */
      if ((sgLastElement & (SCSI_UEXACT32)0x40000000) || (--count == 0))
      {
         break;
      }
   }
   
   return(residueWork);
}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhDownshiftU320ResidueCalc
*
*     Calculate residual length.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:                
*                 Incoming Mode is either 0 or 1
*                  
*********************************************************************/
#if SCSI_DOWNSHIFT_MODE
SCSI_UEXACT32 SCSIhDownshiftU320ResidueCalc (SCSI_HHCB SCSI_HPTR hhcb,
                                             SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;
   void SCSI_LPTR virtualAddress;
   SCSI_BUS_ADDRESS sgPointerSCB;
   SCSI_UEXACT32 sgLen;
   SCSI_BUFFER_DESCRIPTOR SCSI_LPTR sgList;
   SCSI_UEXACT8 sgCacheWork = 0;
   SCSI_UEXACT32 residueWork = 0;
   SCSI_UINT32 count;          /* time out counter */
   int i = 0;
   int currentSGindex;  
        
   /* Return if no data */
#if SCSI_DOWNSHIFT_U320_MODE   
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
   {
      if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,sg_cache_SCB))) & SCSI_DU320_NODATA)
      {
          return(0);
      }
   }
#endif /* SCSI_DOWNSHIFT_U320_MODE */
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)
   {
      if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
           OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,sg_cache_SCB))) & SCSI_DEU320_NODATA)
      {
          return(0);
      }
   }
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */
   /* Put embedded s/g element length into residueWork. */
   /* If CDB length in SCSI_SU320_CDBLENMSK is >0, transfer was not started.  */
   /*    Need to initialize residueWork with embedded s/g element,            */
   /*    Current s/g pointer is from sg_pointer_SCB                           */
   /* Else                                                                    */
   /*    SCSI_SU320_sdata_residue0-3 are used to intitialize residueWork      */
   /*    Current s/g pointer is from sg_pointer_work                          */
#if SCSI_DOWNSHIFT_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
   {
      if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,scdb_length))) & SCSI_DU320_CDBLENMSK)
      {
         /* Get current segment length                                           */
         /*    Its ok to use SCSIhGetAddressBA32 cause we know length is 3 bytes */
         /*    regardless of Bus Address size                                    */
         residueWork = (SCSI_UEXACT32)((SCSI_UEXACT32)SCSIhGetEntity32FromScb(hhcb,
                       (SCSI_UEXACT16)(SCSI_SCB_BASE+
                       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,slength0))) & 0x00FFFFFF);

         /* Get s/g pointer from SCB - this is pointer to s/g cache line.     */
         /* Actual s/g element pointer is index (s/g cache work) + s/g cache  */
         /* line pointer.                                                     */
#if (((OSD_BUS_ADDRESS_SIZE == 32) || (!SCSI_BIOS_SUPPORT)) && (!SCSI_ASPI_SUPPORT))
         sgPointerSCB = SCSI_hGETADDRESSSCB(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
                        OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_sg_pointer_SCB0)));
#else
         ((SCSI_OCTLET SCSI_IPTR)&sgPointerSCB)->u32.order0 = 
            (SCSI_UEXACT32) SCSIhGetEntity32FromScb(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_sg_pointer_SCB0)));
         ((SCSI_OCTLET SCSI_IPTR)&sgPointerSCB)->u32.order1 = 
            (SCSI_UEXACT32) SCSIhGetEntity32FromScb(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_sg_pointer_SCB4)));
#endif /* (((OSD_BUS_ADDRESS_SIZE == 32) || (!SCSI_BIOS_SUPPORT)) && (!SCSI_ASPI_SUPPORT)) */

         sgCacheWork = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,sg_cache_SCB)));

         if (sgCacheWork & SCSI_DU320_ONESGSEG) 
         {
            return(residueWork);
         }
      }
      else
      {
         /* Get current segment length                                           */
         /* Its ok to use SCSIhGetEntity32FromScb cause we know sdata_residue is */
         /*    4 bytes regardless of Bus Address size                            */
         residueWork = (SCSI_UEXACT32)(SCSIhGetEntity32FromScb(hhcb,
                       (SCSI_UEXACT16)(SCSI_SCB_BASE+
                       OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_sdata_residue0)))
                       & 0x00FFFFFF);

         /* Get s/g pointer from SCB - this is pointer to s/g cache line.     */
         /* Actual s/g element pointer is index (s/g cache work) + s/g cache  */
         /* line pointer.                                                     */
#if OSD_BUS_ADDRESS_SIZE == 64
         ((SCSI_OCTLET SCSI_IPTR)&sgPointerSCB)->u32.order0 = 
            (SCSI_UEXACT32) SCSIhGetEntity32FromScb(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_sg_pointer_work0)));
         ((SCSI_OCTLET SCSI_IPTR)&sgPointerSCB)->u32.order1 = 
            (SCSI_UEXACT32) SCSIhGetEntity32FromScb(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_sg_pointer_SCB4)));
#else /* !OSD_BUS_ADDRESS_SIZE == 64 */
#if SCSI_ASPI_SUPPORT
         SCSIhGetAddressScbBA32(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_sg_pointer_work0)));
         sgPointerSCB = hhcb->busAddress;
#else
         sgPointerSCB = SCSIhGetAddressScbBA32(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_sg_pointer_work0)));
#endif /* SCSI_ASPI_SUPPORT */
#endif /* OSD_BUS_ADDRESS_SIZE == 64 */
   
         /* Get s/g cache work */
         sgCacheWork = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_sg_cache_work)));

         if (sgCacheWork & SCSI_DU320_ONESGSEG)
         {
            return(residueWork);
         }
      }
   }
#endif /* SCSI_DOWNSHIFT_U320_MODE */
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
   {
      if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
          OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,scdb_length))) & SCSI_DEU320_CDBLENMSK)
      {
         /* Get current segment length                                           */
         /*    Its ok to use SCSIhGetAddressBA32 cause we know length is 3 bytes */
         /*    regardless of Bus Address size                                    */
         residueWork = (SCSI_UEXACT32)((SCSI_UEXACT32)SCSIhGetEntity32FromScb(hhcb,
                       (SCSI_UEXACT16)(SCSI_SCB_BASE+
                       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,slength0))) & 0x00FFFFFF);

         /* Get s/g pointer from SCB - this is pointer to s/g cache line.     */
         /* Actual s/g element pointer is index (s/g cache work) + s/g cache  */
         /* line pointer.                                                     */
#if (((OSD_BUS_ADDRESS_SIZE == 32) || (!SCSI_BIOS_SUPPORT)) && (!SCSI_ASPI_SUPPORT))
         sgPointerSCB = SCSI_hGETADDRESSSCB(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
                        OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_sg_pointer_SCB0)));
#else
         ((SCSI_OCTLET SCSI_IPTR)&sgPointerSCB)->u32.order0 = 
            (SCSI_UEXACT32) SCSIhGetEntity32FromScb(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_sg_pointer_SCB0)));
         ((SCSI_OCTLET SCSI_IPTR)&sgPointerSCB)->u32.order1 = 
            (SCSI_UEXACT32) SCSIhGetEntity32FromScb(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_sg_pointer_SCB4)));
#endif /* (((OSD_BUS_ADDRESS_SIZE == 32) || (!SCSI_BIOS_SUPPORT)) && (!SCSI_ASPI_SUPPORT))*/

         sgCacheWork = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,sg_cache_SCB)));

         if (sgCacheWork & SCSI_DEU320_ONESGSEG) 
         {
            return(residueWork);
         }
      }
      else
      {
         /* Get current segment length                                           */
         /* Its ok to use SCSIhGetEntity32FromScb cause we know sdata_residue is */
         /*    4 bytes regardless of Bus Address size                            */
         residueWork = (SCSI_UEXACT32)(SCSIhGetEntity32FromScb(hhcb,
                       (SCSI_UEXACT16)(SCSI_SCB_BASE+
                       OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_sdata_residue0)))
                       & 0x00FFFFFF);

         /* Get s/g pointer from SCB - this is pointer to s/g cache line.     */
         /* Actual s/g element pointer is index (s/g cache work) + s/g cache  */
         /* line pointer.                                                     */
#if OSD_BUS_ADDRESS_SIZE == 64
         ((SCSI_OCTLET SCSI_IPTR)&sgPointerSCB)->u32.order0 = 
            (SCSI_UEXACT32) SCSIhGetEntity32FromScb(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_sg_pointer_work0)));
         ((SCSI_OCTLET SCSI_IPTR)&sgPointerSCB)->u32.order1 = 
            (SCSI_UEXACT32) SCSIhGetEntity32FromScb(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_sg_pointer_SCB4)));
#else /* !OSD_BUS_ADDRESS == 64 */
#if SCSI_ASPI_SUPPORT
         SCSIhGetAddressScbBA32(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_sg_pointer_work0)));
         sgPointerSCB = hhcb->busAddress;
#else
         sgPointerSCB = SCSIhGetAddressScbBA32(hhcb,(SCSI_UEXACT16)(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_sg_pointer_work0)));
#endif /* SCSI_ASPI_SUPPORT */
#endif /* OSD_BUS_ADDRESS_SIZE == 64 */
   
         /* Get s/g cache work */
         sgCacheWork = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_sg_cache_work)));

         if (sgCacheWork & SCSI_DEU320_ONESGSEG)
         {
            return(residueWork);
         }
      }
   }
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */
   /* Get the S/G List (its a buffer descriptor) */
   sgList = OSD_GET_SG_LIST(hiob);
   busAddress = sgList->busAddress;
   virtualAddress = sgList->virtualAddress;

   /* Must walk down s/g list until at the same point sequencer */
   /* or time out counter expires.                              */                     
   count=(SCSI_UINT32)0x800000;
   while (1)
   {
      if ((!SCSIhCompareBusAddress(sgPointerSCB,busAddress)) || (--count == 0))
      {
         break;
      }
      else 
      {
         OSD_ADJUST_BUS_ADDRESS(hhcb,&busAddress,2*sizeof(SCSI_BUS_ADDRESS));
         i++;
      }
   }

   /* S/G element is i + index into s/g cache line pointer */
   i += sgCacheWork/((SCSI_UEXACT8)(2*sizeof(SCSI_BUS_ADDRESS)));

   /* sgPointerSCB point to next sg element, need to adjust it to get */
   /* current index. i should always not be equal to 0, though. */
   if (i == 0)
   {
      currentSGindex = i;
   }
   else
   {
      currentSGindex = i-1;
   }

   /* If current s/g element is last one then don't walk down list */
   SCSI_GET_LITTLE_ENDIAN32(hhcb,&sgLen,virtualAddress,
        (currentSGindex*2*sizeof(SCSI_BUS_ADDRESS))+sizeof(SCSI_BUS_ADDRESS));
   if  (sgLen & (SCSI_UEXACT32)0x80000000)
   {
      return(residueWork);
   }

   /* Walk down remainder of s/g list and sum the length portions of the s/g  */
   /* elements. When the delimiter is encountered, or timeout counter expires */
   /* then exit. */
   count=(SCSI_UINT32)0x800000;    
   while (1)
   {
      SCSI_GET_LITTLE_ENDIAN32(hhcb,&sgLen,virtualAddress,
           (i*2*sizeof(SCSI_BUS_ADDRESS))+sizeof(SCSI_BUS_ADDRESS));

      residueWork += (sgLen & 0x00FFFFFF);
      /* to handle one segment and zero in length passed in */
      if ((sgLen & (SCSI_UEXACT32)0x80000000) || (--count == 0))
      {
         break;
      }
      i++;
   }
   return(residueWork);
}
#endif /* SCSI_DOWNSHIFT_MODE */


/*********************************************************************
*
*  SCSIhStandardUpdateExeQ
*
*     Update execution queue and target's execution queue
*     
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob 
*
*  Remarks:       No mode assumptions, restores mode on exit.                 
*                 Assumes sequencer is paused.   
*                 Normally, this routine will be invoked after
*                 an unexpected bus free, a selection timeout
*                 or an active aborted HIOB.
*                  
*********************************************************************/
#if (SCSI_STANDARD_MODE && (!SCSI_DCH_U320_MODE))
void SCSIhStandardUpdateExeQ (SCSI_HHCB SCSI_HPTR hhcb,
                              SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 targTailScb;
   SCSI_UEXACT16 targTailIndex;
   SCSI_UEXACT8 savedMode;
   SCSI_UEXACT16 savedScbPtr;
   SCSI_UEXACT16 exeTailScb;
   SCSI_UEXACT16 scbNext;
   SCSI_UEXACT8 scsiID;
   SCSI_UEXACT16 qExeTargNextScbArray;
   SCSI_UEXACT16 qNextScbArray;

   scsiID = (SCSI_UEXACT8) SCSI_TARGET_UNIT(hiob)->scsiID;

   /* Save the mode register */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Set mode 3 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Check if q_exetarg_tail matches this SCB, if so then
    * we need to clear it out as the sequencer only clears it
    * out when a selection is completed sucessfully.
    */
   targTailIndex = SCSI_SU320_Q_EXETARG_TAIL + (2 * scsiID);
   targTailScb =
      (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(targTailIndex)) |
      ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(targTailIndex+1)) << 8);

   if (targTailScb == hiob->scbNumber)
   {
      /* Clear the entry in q_exetarg_tail table */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(targTailIndex + 1),
                         (SCSI_UEXACT8)(SCSI_NULL_SCB));
   }
   else
   {
      /* More than one SCB in target execution queue. */
      /* Append remaining SCBs in the target's execution
       * queue to the end of the execution queue for all
       * targets. Note that at selection time the whole 
       * target execution queue was removed.
       */

      /* Save SCBPTR */
      savedScbPtr =
         (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
         ((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1)) << 8);

      /* Set Mode 3 SCBPTR to hiob->scbNumber to access this scb. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber >> 8));

      /* Assign the register mapped SCB field (SCBARRAY) variables. */  
      qExeTargNextScbArray =
         (SCSI_UEXACT16)(SCSI_SCB_BASE + SCSI_hQEXETARGNEXTOFFSET(hhcb));
      qNextScbArray =
         (SCSI_UEXACT16)(SCSI_SCB_BASE + SCSI_hQNEXTOFFSET(hhcb));

      /* Get the q_exetarg_next field of SCB. */
      scbNext =
         (SCSI_UEXACT16)
            OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray)) |
         ((SCSI_UEXACT16)
            (OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray+1))) << 8);

      /* Set SCBPTR to this SCB. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)scbNext);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(scbNext >> 8));
    
      /* Set q_next field to Null as we are queuing to the end of the
       * execution queue.
       */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1),
                         (SCSI_UEXACT8)SCSI_NULL_SCB);

      /* Now put this SCB (and any linked SCBs into the Execution queue). */ 
      /* Get scbNumber in Head of Execution queue */
      if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1)) ==
          (SCSI_UEXACT8) SCSI_NULL_SCB)
      {
         /* Execution queue is empty. */
         /* Assign scb to exe_head & (later exe_tail). */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD),
                            (SCSI_UEXACT8)scbNext);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1),
                            (SCSI_UEXACT8)(scbNext >> 8));
      }
      else
      {
         /* Put scb on execution tail. */
         /* Get current tail. */
         exeTailScb = 
            ((SCSI_UEXACT16)
                OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL)) |
             ((SCSI_UEXACT16)
                (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL+1))) << 8));

         /* Set SCBPTR to this SCB. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                       (SCSI_UEXACT8)exeTailScb);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                       (SCSI_UEXACT8)(exeTailScb >> 8));

         /* Update q_next field of this SCB to q_exe_next of the removed SCB. */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray),
                            (SCSI_UEXACT8)scbNext);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1),
                            (SCSI_UEXACT8)(scbNext >> 8));
      }
      
      /* Lastly update Q_EXE_TAIL to q_exe_next of the removed SCB. */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL),
                         (SCSI_UEXACT8)scbNext);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL+1),
                         (SCSI_UEXACT8)(scbNext >> 8));
  
      /* Restore Mode 3 SCBPTR. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)savedScbPtr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(savedScbPtr >> 8));
   }

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
}
#endif /* (SCSI_STANDARD_MODE && (!SCSI_DCH_U320_MODE)) */

/*********************************************************************
*
*  SCSIhDchU320UpdateExeQ
*
*     Update execution queue and target's execution queue
*     
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob 
*
*  Remarks:       No mode assumptions, restores mode on exit.                 
*                 Assumes sequencer is paused.   
*                 Normally, this routine will be invoked after
*                 an unexpected bus free, a selection timeout
*                 or an active aborted HIOB.
*                  
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320UpdateExeQ (SCSI_HHCB SCSI_HPTR hhcb,
                                  SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 targTailScb;
   SCSI_UEXACT16 targTailIndex;
   SCSI_UEXACT8 savedMode;
   SCSI_UEXACT16 savedScbPtr;
   SCSI_UEXACT16 exeTailScb;
   SCSI_UEXACT16 scbNext;
   SCSI_UEXACT8 scsiID;

   scsiID = (SCSI_UEXACT8) SCSI_TARGET_UNIT(hiob)->scsiID;

   /* Save the mode register */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Set mode 3 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Check if q_exetarg_tail matches this SCB, if so then
    * we need to clear it out as the sequencer only clears it
    * out when a selection is completed sucessfully.
    */
   targTailIndex = SCSI_DCHU320_Q_EXETARG_TAIL + (2 * scsiID);
   targTailScb =
      (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(targTailIndex)) |
      ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(targTailIndex+1)) << 8);

   if (targTailScb == hiob->scbNumber)
   {
      /* Clear the entry in q_exetarg_tail table */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(targTailIndex + 1),
                         (SCSI_UEXACT8)(SCSI_NULL_SCB));
   }
   else
   {
      /* More than one SCB in target execution queue. */
      /* Append remaining SCBs in the target's execution
       * queue to the end of the execution queue for all
       * targets. Note that at selection time the whole 
       * target execution queue was removed.
       */

      /* Save SCBPTR */
      savedScbPtr =
         (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
         ((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1)) << 8);

      /* Set Mode 3 SCBPTR to hiob->scbNumber to access this scb. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)hiob->scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(hiob->scbNumber >> 8));
 
      /* Get the q_exetarg_next field of SCB. */
      scbNext =
         (SCSI_UEXACT16)
            OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE +
                           OSDoffsetof(SCSI_SCB_DCH_U320,
                                       SCSI_DCHU320_q_exetarg_next))) |
         ((SCSI_UEXACT16)
            (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE +
                            OSDoffsetof(SCSI_SCB_DCH_U320,
                                        SCSI_DCHU320_q_exetarg_next)+1))) << 8);
      /* Set SCBPTR to this SCB. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)scbNext);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(scbNext >> 8));
    
      /* Set q_next field to Null as we are queuing to the end of the
       * execution queue.
       */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE +
                                     OSDoffsetof(SCSI_SCB_DCH_U320,
                                                 SCSI_DCHU320_q_next)+1),
                         (SCSI_UEXACT8)SCSI_NULL_SCB);

      /* Now put this SCB (and any linked SCBs into the Execution queue). */ 
      /* Get scbNumber in Head of Execution queue */
      if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1)) ==
          (SCSI_UEXACT8) SCSI_NULL_SCB)
      {
         /* Execution queue is empty. */
         /* Assign scb to exe_head & (later exe_tail). */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD),
                            (SCSI_UEXACT8)scbNext);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1),
                            (SCSI_UEXACT8)(scbNext >> 8));
      }
      else
      {
         /* Put scb on execution tail. */
         /* Get current tail. */
         exeTailScb = 
            ((SCSI_UEXACT16)
                OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL)) |
             ((SCSI_UEXACT16)
                (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL+1))) << 8));

         /* Set SCBPTR to this SCB. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                       (SCSI_UEXACT8)exeTailScb);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                       (SCSI_UEXACT8)(exeTailScb >> 8));

         /* Update q_next field of this SCB to q_exe_next of the removed SCB. */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)),
                        (SCSI_UEXACT8)scbNext);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1),
                        (SCSI_UEXACT8)(scbNext >> 8));
      }
      
      /* Lastly update Q_EXE_TAIL to q_exe_next of the removed SCB. */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL),
                         (SCSI_UEXACT8)scbNext);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL+1),
                         (SCSI_UEXACT8)(scbNext >> 8));
  
      /* Restore Mode 3 SCBPTR. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                    (SCSI_UEXACT8)savedScbPtr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                    (SCSI_UEXACT8)(savedScbPtr >> 8));
   }

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
}
#endif /* SCSI_DCH_U320_MODE */
/*********************************************************************
*
*  SCSIhDownshiftU320UpdateExeQ
*
*     Update execution queue and target execution queue
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob 
*
*  Remarks:       No mode assumptions, restores mode on exit.            
*                 Assumes sequencer is paused.                  
*                 Normally, this routine will be invoked after
*                 an unexpected bus free, a selection timeout
*                 or an active aborted HIOB.
*                  
*********************************************************************/
#if SCSI_DOWNSHIFT_MODE
void SCSIhDownshiftU320UpdateExeQ (SCSI_HHCB SCSI_HPTR hhcb,
                                   SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 targTailScb;
   SCSI_UEXACT16 targTailIndex;
   SCSI_UEXACT8 scsiID;
   SCSI_UEXACT8 savedMode;

   scsiID = (SCSI_UEXACT8) SCSI_TARGET_UNIT(hiob)->scsiID;

   /* Save the mode register */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Set mode 3 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Check if q_exetarg_tail matches this SCB, if so then
    * we need to clear it out as the sequencer only clears it
    * out when a selection is completed sucessfully.
    * As downshift mode is single tasking there is handling
    * requiredno for multiple I/Os in the target execution
    * queue.
    */
   targTailIndex = SCSI_DU320_Q_EXETARG_TAIL + (2 * scsiID);
   targTailScb =
      (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(targTailIndex)) |
      ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(targTailIndex+1)) << 8);

   if (targTailScb == hiob->scbNumber)
   {
      /* Clear the entry in q_exetarg_tail table */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(targTailIndex + 1),
                         (SCSI_UEXACT8)(SCSI_NULL_SCB));
   }

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
}
#endif /* SCSI_DOWNSHIFT_MODE */
    
/*********************************************************************
*
*  SCSIhBusWasDead
*
*     Check if scsi bus still alive.
*
*  Return Value:  0 : scsi bus is alive
*                 1 : scsi bus is dead
*                  
*  Parameters:    hhcb
*
*  Remarks:  If only operating in target mode then
*            this routine does not check the SCSI bus signals.      
*                  
*********************************************************************/
SCSI_UEXACT8 SCSIhBusWasDead (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 scsisig;
   SCSI_UEXACT32 timer;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 status = 1; /* default to dead */ 
   SCSI_UEXACT8 savedMode;

   /* Clear the bus hung indicator */
   SCSI_hSETBUSHUNG(hhcb,0);

   /* check if pause chip necessary */
   if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);      
   }

   /* Save the mode register */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Set mode 3 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Check if any of the control signals change; if so, bus is alive, else */
   /* it is dead */
   scsisig = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI));
   if (scsisig
#if SCSI_TARGET_OPERATION   
       && hhcb->SCSI_HF_initiatorMode
      /* If target mode only don't check bus signals as we may be 
       * selected shortly after a bus reset.
       */
#endif /* SCSI_TARGET_OPERATION */
       )
   {
      /* Multiple by two as timer is in increments of 500 usecs
       * and timeout is in milliseconds.
       */
      timer = (SCSI_UINT32)(hhcb->ioChannelFailureTimeout * 2);
      while (timer)
      {
         /* 500 uSec delay */
         SCSIhDelayCount25us(hhcb, SCSI_hGETTIMERCOUNT(hhcb,500));

         if (scsisig != OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)))
         {
            status = 0;  /* scsi bus is alive */
            break;
         }
         timer--;
      }
   }
   /* Check if RST signal goes off; if so, bus is alive, else it is dead */
   else if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1))) & SCSI_SCSIRSTI)
   {
      /* Multiple by two as timer is in increments of 500 usecs
       * and timeout is in milliseconds.
       */
      timer = (SCSI_UINT32)(hhcb->ioChannelFailureTimeout * 2);
      while (timer)
      {
         /* 500 uSec delay */
         SCSIhDelayCount25us(hhcb, SCSI_hGETTIMERCOUNT(hhcb,500));
         
         /*Clear reset in indicator*/
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRSCSIRSTI);
         
         if (!((OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1))) & SCSI_SCSIRSTI))
         {
            status = 0;  /* scsi bus is alive */
            break;
         }
         timer--;
      }
   }
   /* Bus is quiet; so assume it is alive */
   else
   {
      status = 0;  /* scsi bus is alive */
   }

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
   
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }

   if (status)
   {
      SCSI_hSETBUSHUNG(hhcb,1);
   }

   return(status);
}

/*********************************************************************
*
*  SCSIhHandleHwErrInterrupt
*
*     This routine handles the Hard Error interrupt (HWERRINT).
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*
*  Remarks:       Assume mode 3 upon entry.
*                  
*********************************************************************/
#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
void SCSIhHandleHwErrInterrupt (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 errorRegister;

#if SCSI_INTERNAL_PARITY_CHECK
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_ERROR)) & SCSI_DPARERR)
   {
      SCSIhDataParity(hhcb);
   }
/*   if (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & SCSI_IOERR)*/
#endif /* SCSI_INTERNAL_PARITY_CHECK */

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRERR),
                 SCSI_CLRCIOPARERR | SCSI_CLRMPARERR | SCSI_CLRDPARERR);

   errorRegister = OSD_INEXACT8(SCSI_AICREG(SCSI_ERROR));

   /* the statement below needs to be verified with HW */
   if (errorRegister & SCSI_SQPARERR)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0),
         OSD_INEXACT8(SCSI_AICREG(SCSI_SEQCTL0)) | SCSI_LOADRAM);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRERR), SCSI_CLRSQPARERR);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0),
         OSD_INEXACT8(SCSI_AICREG(SCSI_SEQCTL0)) & ~SCSI_LOADRAM);
   }

#if !SCSI_DCH_U320_MODE
   if (errorRegister & SCSI_DSCTMOUT)
   {
      /* Set mode 0 */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE0);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCH0SPLTSTAT0), SCSI_RXSPLTRSP);

      /* Set mode 1 */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE1);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCH1SPLTSTAT0), SCSI_RXSPLTRSP);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SG01SPLTSTAT0), SCSI_RXSPLTRSP);

      /* Set mode 2 */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CMCSPLTSTAT0), SCSI_RXSPLTRSP);

      /* Set mode 3 */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_OVLYSPLTSTAT0), SCSI_RXSPLTRSP);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRERR), SCSI_CLRDSCTMOUT);
   }
#endif /* !SCSI_DCH_U320_MODE */

#if !SCSI_INTERNAL_PARITY_CHECK
   SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID72);
#endif /* !SCSI_INTERNAL_PARITY_CHECK */

   if (errorRegister & SCSI_SQPARERR)
   {
#if !SCSI_BIOS_SUPPORT
      SCSI_hSETUPSEQUENCER(hhcb);
      SCSIhStartToRunSequencer(hhcb);
#endif /* !SCSI_BIOS_SUPPORT */
   }
}
#endif /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */

/*********************************************************************
*
*  SCSIhDataParity
*
*     This routine is used for debugging purpose.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*
*  Remarks:       Assume mode 3 upon entry.
*                  
*********************************************************************/
#if SCSI_INTERNAL_PARITY_CHECK
void SCSIhDataParity (SCSI_HHCB SCSI_HPTR hhcb)
{
}
#endif /* SCSI_INTERNAL_PARITY_CHECK */

/*********************************************************************
*
*  SCSIhHandleSplitInterrupt
*
*     This routine handles the Split Interrupt (SPLINT).
*
*  Return Value:  None
*
*  Parameters:    hhcb
*
*  Remarks:
*
*********************************************************************/
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
void SCSIhHandleSplitInterrupt (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 pcixStatus;
   SCSI_UEXACT8 pcixErrorStatHigh,pcixErrorStatLow;
   SCSI_UEXACT8 modeSave,errorMode;
   SCSI_UEXACT8 pcixSlaveError;
   SCSI_UEXACT8 postEventFlag = 0;
   SCSI_UEXACT16 scbNumber;
   SCSI_HIOB SCSI_IPTR hiob;

   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

#if SCSI_BIOS_SUPPORT
   SCSIHReadScratchRam(hhcb,
                       (SCSI_UEXACT8 SCSI_SPTR) &errorMode,
                       (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ORIGINALMODE),
                       (SCSI_UEXACT16) 1);
#else
   errorMode =  hhcb->SCSI_HP_originalMode;
#endif /* SCSI_BIOS_SUPPORT */

   SCSI_hSETMODEPTR(hhcb,scsiRegister,errorMode);

   scbNumber = ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb)+1))) << 8) |
                (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb))));

   if (SCSI_IS_NULL_SCB(scbNumber))
   {
      hiob = SCSI_NULL_HIOB;
   }
   else
   {
#if SCSI_BIOS_SUPPORT
      SCSIHReadScratchRam(hhcb,
                       (SCSI_UEXACT8 SCSI_SPTR) &hiob,
                       (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ACTIVE_HIOB),
                       (SCSI_UEXACT16) 4);
#elif SCSI_DOWNSHIFT_MODE
      hiob = SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET];
#else 
      hiob = SCSI_ACTPTR[scbNumber];
#endif /* SCSI_BIOS_SUPPORT */
   }

   /* Set Mode 4 - to access PCI-X Status register */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

   /* reg 19Bh */
   pcixErrorStatHigh = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_PCIX_STATUS3));

   /* reg 19Ah */
   pcixErrorStatLow = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_PCIX_STATUS2));

   /**************************************************************************** 
      Error cases:

      1) A SPLTINT will occur if 1 of 4 bits is set in register 96h (mode 0-3)
         or register 9Eh (mode 0 and 1). 

         SCBCERR, SCADERR, RXOVERUN, RXSCEMSG

      2) A SPLTINT will occur if the unexpected completion bit is set in the 
         PCI-X Status register 19Bh-198H (mode 4).

         UNEXPSC

      3) A SPLTINT will occur if 1 of 3 bits is set in register 19Dh (mode 4).

         DPR_SPLT, RXSCITA, RXSCIMA

     ****************************************************************************/

   /* CASE 1: read regs 96h (modes 0-3) and reg 9Eh (mode 0 and 1) */
   pcixStatus = SCSIhGetPcixStatus(hhcb);

   /* CASE 2: Test for an unexpected completion message. PCI-X reg 19A (mode 4) */
   if (pcixErrorStatLow & SCSI_UNEXPSC)   /*  Unepected completion message */
   {
      /* this could definitely change since no task can be associated with it */
      postEventFlag = 0x08;
   }

   /* CASE 3: read Slave Split Status register 19D (mode 4) */
   pcixSlaveError = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+ SCSI_SLVSPLTSTAT));
   
   /* Begin with all status bits found in regs 96h or 9Eh */
   if (pcixStatus & SCSI_PCIX_SPLIT_ERRORS)
   {
      postEventFlag = 0x08;
   }
   
   if (!postEventFlag && (pcixSlaveError & SCSI_PCIX_SLAVE_ERRORS))
   {
      postEventFlag = 0x08;
   }

/*********************************************************************************
   Error processing could change depending on the requirement of an OSM. For now
   our approach will be to use the 'big hammer' approach by reporting this error
   via an EVENT message. All IOBs are to be aborted and SCSI bus reset. The OSM 
   must follow explicit recovery instruction before the HIM is capable of normal 
   operations. Refer to the Common Hardware Interface Manual (CHIM V1.11) for a 
   detailed description with regards to the entire error recovery procedure.
**********************************************************************************/

   /********************************************************************
      The variable, 'postEventFlag', is set to determine how the
      HIM error handler will clean up and report a specific type of
      PCI error.

     ** NOTE: Cases 1,2, and 4 are NOT CURRENTLY SUPPORTED **

      postEventFlag = 1 : Do not report or abort any IOB(s) allegedly
                          associated with the cause of the PCI error.

      postEventFlag = 2 : Send a notification Event only.

      postEventFlag = 4 : Abort only the IOB that was active or believed to be
                          cause for the error.

      postEventFlag = 8 : Send an OSM Freeze, abort all HA activity, send
                           a 'PCI Aborted Event' to the OSM followed by
                           an OSM Unfreeze event notification.
     **********************************************************************/

   switch (postEventFlag)
   {
      case 0x01:
         /* unsupported */
         /* do nothing */
         break;

      case 0x02:
         /* unsupported */
         break;

      case 0x04:
         /* unsupported */
         break;

      case 0x08:
         /* Set event reason field */
         hhcb->freezeEventReason = SCSI_AE_ABORTED_PCIX_SPLIT_ERROR;
         SCSI_ASYNC_EVENT(hhcb, SCSI_AE_OSMFREEZE); /* AEN call back to RSM layer */

         if (SCSIhResetScsi(hhcb))
         {
            SCSI_hSETBUSHUNG(hhcb,1);
         }

         /* Delay resetDelay msecs after scsi reset for slow devices to settle down. */
         SCSIhDelayCount25us(hhcb, SCSI_hGETTIMERCOUNT(hhcb,(hhcb->resetDelay*1000)));
         
         /* Block upper layers from issuing Establish Connection HIOBs */
         SCSI_hTARGETHOLDESTHIOBS(hhcb,1);
         
         /* set haStat to indicate this hiob was active when error detected */
         if (hiob != SCSI_NULL_HIOB)
         {
            hiob->haStat = SCSI_HOST_PCI_OR_PCIX_ERROR;
         }
         SCSI_hABORTCHANNEL(hhcb, SCSI_HOST_ABORTED_PCIX_SPLIT_ERROR); /* abort all active cmds */
         
         /* Unblock upper layers from issuing Establish Connection HIOBs */
         SCSI_hTARGETHOLDESTHIOBS(hhcb,0);
         
         /* Async. Event to OSM */
         SCSI_ASYNC_EVENT(hhcb, SCSI_AE_ABORTED_PCIX_SPLIT_ERROR);
         
         SCSI_ASYNC_EVENT(hhcb, SCSI_AE_OSMUNFREEZE);    
         break;
         
      default:
         break;
   }
    
   /* Restore mode to original mode on entry. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
}
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */

/*********************************************************************
*
*  SCSIhHandleRBIError
*
*     This routine examines the RBI error detected by the hardware and
*     optionally clears the error. The OSM is informed of the PCI error
*     via an asynchronous event.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*
*  Remarks:       Currently this is a stub for implementation TBD
*
*
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhHandleRBIError (SCSI_HHCB SCSI_HPTR hhcb)
{

}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhHandlePCIError
*
*     This routine examines the PCI error detected by the hardware and
*     optionally clears the error. The OSM is informed of the PCI error
*     via an asynchronous event.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*
*  Remarks:       This routine may require futher enhancements. An OSM
*                 interface spec change is required. Saves and restores
*                 the mode pointer register from the caller.
*
*********************************************************************/
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
void SCSIhHandlePCIError (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 pciEvent,pciHostStatus;
   SCSI_UEXACT8 pciErrStatus;
   SCSI_UEXACT8 modeSave,errorMode;
   SCSI_UEXACT8 pcixErrStatusExist;
   SCSI_UEXACT8 pciErrorMaster, pciErrorTarget,pciDiscard; 
   SCSI_UEXACT8 dfcntrl,ccsgctl,ccscbctl;
   SCSI_BOOLEAN errorDetected = 0;
   SCSI_UEXACT16 scbNumber;
   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_UINT32 j;

   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

#if SCSI_BIOS_SUPPORT
   SCSIHReadScratchRam(hhcb,
                       (SCSI_UEXACT8 SCSI_SPTR) &errorMode,
                       (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ORIGINALMODE),
                       (SCSI_UEXACT16) 1);
#else
   errorMode =  hhcb->SCSI_HP_originalMode;
#endif /* SCSI_BIOS_SUPPORT */

   SCSI_hSETMODEPTR(hhcb,scsiRegister,errorMode);

   scbNumber = ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb)+1))) << 8) |
                (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb))));

   if (SCSI_IS_NULL_SCB(scbNumber))
   {
      hiob = SCSI_NULL_HIOB;
   }
   else
   {
#if SCSI_BIOS_SUPPORT
      SCSIHReadScratchRam(hhcb,
                       (SCSI_UEXACT8 SCSI_SPTR) &hiob,
                       (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ACTIVE_HIOB),
                       (SCSI_UEXACT16) 4);
#elif SCSI_DOWNSHIFT_MODE
      hiob = SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET];
#else   
      hiob = SCSI_ACTPTR[scbNumber];
#endif /* SCSI_BIOS_SUPPORT */
   }

   /* Set Mode 4 to access PCI Config Space - reg 107h (CMD/STS) */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

   pciErrStatus = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_STATUS_HI_REG));

   /* set Event and haStat to PCI error */
   pciEvent = SCSI_AE_ABORTED_PCI_ERROR;
   pciHostStatus = SCSI_HOST_ABORTED_PCI_ERROR;

   /* There are numerous registers that can indicate the root
    * cause associated with a PCIINT condition. If we are currently
    * operating on a PCI-X bus begin by checking if the 'STA' bit is 
    * set in the Config/Status space. If so read,save and clear the 
    * PCI-X status register(s) associated with this condition.
    */

   /* Check if PCI-X bus */
   if ((OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_DEVSTATUS0_REG)) & SCSI_PCI33_66) != SCSI_PCI33_66)
   {
      if (pciErrStatus & SCSI_PCI_STA)
      {
         /* Error status may be set in Split Status register 96h or 9Eh */
         if ((pcixErrStatusExist = SCSIhGetPcixStatus(hhcb)) &
              SCSI_STAETERM)
         {
            errorDetected = 0x01;
            pciEvent = SCSI_AE_ABORTED_PCIX_SPLIT_ERROR;
            pciHostStatus = SCSI_HOST_ABORTED_PCIX_SPLIT_ERROR;
         }
      }
   }

   /* A0 - DFF0 */
   if (pciDiscard = OSD_INEXACT8(SCSI_AICREG(SCSI_PCISTATDF0)))
   {
      pciErrorMaster = pciDiscard;

      if (pciDiscard & (SCSI_PCI_RCVDMABRT | SCSI_PCI_RCVDTABRT))
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE0);
         if ((dfcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL))) & SCSI_HDMAENACK)
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFCNTRL), dfcntrl & ~SCSI_HDMAEN);
            j = 0;
            while ((OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL)) & SCSI_HDMAENACK) &&
                   (j++ < (SCSI_UINT32)SCSI_REGISTER_TIMEOUT_COUNTER))
            {
               OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
            }
         }
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      }

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCISTATDF0), pciDiscard);
   }

   /* A1 - DFF1 */
   if (pciDiscard = OSD_INEXACT8(SCSI_AICREG(SCSI_PCISTATDF1)))
   {
      pciErrorMaster |= pciDiscard;      

      if (pciDiscard & (SCSI_PCI_RCVDMABRT | SCSI_PCI_RCVDTABRT))
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE1);
         if ((dfcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL))) & SCSI_HDMAENACK)
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFCNTRL), dfcntrl & ~SCSI_HDMAEN);
            j = 0;
            while ((OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL)) & SCSI_HDMAENACK) &&
                   (j++ < (SCSI_UINT32)SCSI_REGISTER_TIMEOUT_COUNTER))
            {
               OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
            }
         }
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      }

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCISTATDF1), pciDiscard);
   }

   /* A2 - SG01 */   
   if (pciDiscard = OSD_INEXACT8(SCSI_AICREG(SCSI_PCISTATSG)))
   {
      pciErrorMaster |= pciDiscard;

      if (pciDiscard & (SCSI_PCI_RCVDMABRT | SCSI_PCI_RCVDTABRT))
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE0);
         if ((ccsgctl = OSD_INEXACT8(SCSI_AICREG(SCSI_CCSGCTL))) & SCSI_CCSGENACK)
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSGCTL), ccsgctl & ~SCSI_CCSGEN);
            j = 0;
            while ((OSD_INEXACT8(SCSI_AICREG(SCSI_CCSGCTL)) & SCSI_CCSGENACK) &&
                   (j++ < (SCSI_UINT32)SCSI_REGISTER_TIMEOUT_COUNTER))
            {
               OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
            }
         }

         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE1);
         if ((ccsgctl = OSD_INEXACT8(SCSI_AICREG(SCSI_CCSGCTL))) & SCSI_CCSGENACK)
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSGCTL), ccsgctl & ~SCSI_CCSGEN);
            j = 0;
            while ((OSD_INEXACT8(SCSI_AICREG(SCSI_CCSGCTL)) & SCSI_CCSGENACK) &&
                   (j++ < (SCSI_UINT32)SCSI_REGISTER_TIMEOUT_COUNTER))
            {
               OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
            }
         }

         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      }

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCISTATSG), pciDiscard);
   }

   /* A3 - CMC */
   if (pciDiscard = OSD_INEXACT8(SCSI_AICREG(SCSI_PCISTATCMC)))
   {
      pciErrorMaster |= pciDiscard;

      if (pciDiscard & (SCSI_PCI_RCVDMABRT | SCSI_PCI_RCVDTABRT))
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);
         if ((ccscbctl = OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL))) & SCSI_CCSCBENACK)
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSCBCTL), ccscbctl & ~SCSI_CCSCBEN);
            j = 0;
            while ((OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL)) & SCSI_CCSCBENACK) &&
                   (j++ < (SCSI_UINT32)SCSI_REGISTER_TIMEOUT_COUNTER))
            {
               OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
            }
         }
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      }

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCISTATCMC), pciDiscard);
   }

   /* A4 - OVLY */
   if (pciDiscard = OSD_INEXACT8(SCSI_AICREG(SCSI_PCISTATOVLY)))
   {
      pciErrorMaster |= pciDiscard;
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCISTATOVLY), pciDiscard);
   }

   /* A6 - MSI */
   if (pciDiscard = OSD_INEXACT8(SCSI_AICREG(SCSI_PCISTATMSI)))
   {
      pciErrorMaster |= pciDiscard;
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCISTATMSI), pciDiscard);
   }

   /* A7 - TARG */
   if (pciErrorTarget = OSD_INEXACT8(SCSI_AICREG(SCSI_PCISTATTARG)))
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCISTATTARG), pciErrorTarget);
   }

   /* Test if a PCI-X error has already set actionRequired to an Abort condition */
   if (!errorDetected)
   {
      /* Master PCI status check */
      if (pciErrorMaster)
      {
         if (pciErrorMaster & SCSI_PCI_MASTER_ERRORS)
         {
            errorDetected = 0x01;
         }
      }
      /* Target PCI status check */
      if (!errorDetected && pciErrorTarget)
      {
         if (pciErrorTarget & SCSI_PCI_TARGET_ERRORS)
         {
            errorDetected = 0x01;
         }
      }
   }
   
   /* Set event reason field */
   hhcb->freezeEventReason = pciEvent;
   
   SCSI_ASYNC_EVENT(hhcb, SCSI_AE_OSMFREEZE); /* AEN call back to RSM layer */

   if (SCSIhResetScsi(hhcb))
   {
      SCSI_hSETBUSHUNG(hhcb,1);
   }

   /* Delay resetDelay msecs after scsi reset for slow devices to settle down. */
   SCSIhDelayCount25us(hhcb, SCSI_hGETTIMERCOUNT(hhcb,(hhcb->resetDelay*1000)));

#if !SCSI_DOWNSHIFT_MODE
   /* Block upper layers from issuing Establish Connection HIOBs */
   SCSI_hTARGETHOLDESTHIOBS(hhcb,1);
#endif /* !SCSI_DOWNSHIFT_MODE */
  
   /* If valid hiob ptr set the host status to indicate this hiob was
    * active when the PCI error was detected.
    */ 
   if (hiob != SCSI_NULL_HIOB)
   {
      hiob->haStat = SCSI_HOST_PCI_OR_PCIX_ERROR;
   }
   /* For now use the 'big hammer' approach by aborting all tasks */
   SCSI_hABORTCHANNEL(hhcb, pciHostStatus);
   
#if !SCSI_DOWNSHIFT_MODE
   /* Unblock upper layers from issuing Establish Connection HIOBs */
   SCSI_hTARGETHOLDESTHIOBS(hhcb,0);
#endif /* !SCSI_DOWNSHIFT_MODE */

   /* Async. Event to OSM */
   SCSI_ASYNC_EVENT(hhcb,pciEvent);
   
   SCSI_ASYNC_EVENT(hhcb, SCSI_AE_OSMUNFREEZE);
   
   /* Restore mode to original mode on entry. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
   
}
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */


/*********************************************************************
*
*  SCSIhPhysicalError
*
*     Handle SCSI physical errors (Parity, CRC, DG, DT)
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Assumption that we are in mode 3.
*
*********************************************************************/
void SCSIhPhysicalError (SCSI_HHCB SCSI_HPTR hhcb,
                         SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
#if !(SCSI_NEGOTIATION_PER_IOB && SCSI_BIOS_SUPPORT)
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit;
#endif /* !(SCSI_NEGOTIATION_PER_IOB && SCSI_BIOS_SUPPORT) */
   SCSI_UEXACT8 sxfrctl1;
   SCSI_UEXACT8 perrdiag;
#if !SCSI_DCH_U320_MODE
   SCSI_UEXACT8 negodata3;
#endif /* !SCSI_DCH_U320_MODE */
   SCSI_UEXACT8 phase;
#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT)
   SCSI_UEXACT8 lqistate;
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT) */

#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT)
   /* Harpoon2 Rev A3 has a problem that the Packetized bit (bit 7)  */
   /* of register LQISTAT2 is not correctly implemented.  This might */
   /* causes the wrong status of the current SCSI connection. The    */
   /* details of this hardware problem can be found in the Razor     */
   /* database entry #653.                                           */
   /* To workaround this problem CHIM will check LQISTATE register   */
   /* to decided packetized or non-packetized mode.  A zero value    */
   /* means non-packetized.  A non-zero means packetized.            */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   lqistate = OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTATE));
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   if (lqistate != 0x00)
   {
      /* For Harpoon2 Rev A, BUSFREE bit could be leftover from previous   */
      /* connections in packetized mode.  It does not generate scsi        */
      /* interrupt in this case.  When a real scsi interrupt occurs later, */
      /* CHIM will not be able to tell if the interrupt is caused by       */
      /* BUSFREE or not.  One case is SCSIPERR happens while BUSFREE is    */
      /* leftover status.  Here, we detect this situation and clears       */
      /* BUSFREE status.                                                   */
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & SCSI_BUSFREE)
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRBUSFREE);
      }

      /* Handle CRC error for LQ packet or non-LQ packet */
      SCSIhPacketizedPhysicalError(hhcb,hiob);
      return;
   }
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT) */

   if (hiob == SCSI_NULL_HIOB)
   {
      sxfrctl1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), (sxfrctl1 & ~SCSI_ENSPCHK));
     /* don't turn on parity if per iob parity checking. */
     /* we need parity off in bus free.                  */
#if !SCSI_PARITY_PER_IOB
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), (sxfrctl1 | SCSI_ENSPCHK));
#endif /* !SCSI_PARITY_PER_IOB */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1),
                    (SCSI_CLRSCSIPERR | SCSI_CLRATNO));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), SCSI_CLRSCSIINT);

      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID12);
      return;
   }
   else
   {
#if !SCSI_DOWNSHIFT_MODE
      if (hiob->SCSI_IF_freezeOnError &&
          !(SCSI_TARGET_UNIT(hiob)->targetControl->freezeMap &
            SCSI_FREEZE_HIPRIEXISTS))
      {
         SCSI_hFREEZEHWQUEUE(hhcb,hiob,SCSI_AE_FREEZEONERROR_START);
      }
#endif /* !SCSI_DOWNSHIFT_MODE */

      /* Setup "Initiator Detected Error" message */
      hiob->SCSI_IP_workArea[SCSI_MAX_WORKAREA-1] = SCSI_MSG05;
      
      if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_BUSPHASE) == SCSI_MIPHASE)
      {
         /* If parity error detected during msg in phase */
         /* then the initiator will send out the Message Parity Error message */
         hiob->SCSI_IP_workArea[SCSI_MAX_WORKAREA-1] = SCSI_MSG09;
      }

      /* Save contents of physical error diagnosis register */
      /* This register cleared when stat1 SCSI_SCSIPERR cleared */
      perrdiag = OSD_INEXACT8(SCSI_AICREG(SCSI_PERRDIAG));
      if (perrdiag & (SCSI_DTERR | SCSI_DGFORMERR))
      {
#if (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)
         if ((perrdiag & SCSI_DGFORMERR) && (SCSI_hHARPOON_REV_A(hhcb)))
         {
            /* Workaround for Razor entry #714 where a non-zero
             * offset is carried formward to the next connection 
             * when a connection ends abnormally. This may
             * occur when a DG format error is reported.
             * Only solution is to reset the ASIC via a SCSI bus
             * reset.
             */
            sxfrctl1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), (sxfrctl1 & ~SCSI_ENSPCHK));
            /* don't turn on parity if per iob parity checking. */
            /* we need parity off in bus free                   */
#if !SCSI_PARITY_PER_IOB
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), (sxfrctl1 | SCSI_ENSPCHK));
#endif /* !SCSI_PARITY_PER_IOB */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1),
                          (SCSI_CLRSCSIPERR | SCSI_CLRATNO));
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), SCSI_CLRSCSIINT);

            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID67);
            return;
         }
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE) */
         
         /* Need to send abort task message to target */
         if ((hiob->SCSI_IF_tagEnable)  && (!hiob->SCSI_IF_disallowDisconnect))
         {
            /* Abort tagged command */
            hiob->SCSI_IP_workArea[SCSI_MAX_WORKAREA-1] = SCSI_MSG0D;
         }
         else
         {
            /* Abort non-tagged command */
            hiob->SCSI_IP_workArea[SCSI_MAX_WORKAREA-1] = SCSI_MSG06;
         }
      }

#if !SCSI_DCH_U320_MODE
      if (perrdiag & SCSI_AIPERR)
      {
         /* Disable AIP checking, obsolete.  */
         negodata3 = OSD_INEXACT8(SCSI_AICREG(SCSI_NEGODATA3));
         negodata3 &= (SCSI_UEXACT8)(~(SCSI_ENAIP));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA3), negodata3);
      }

#endif /* !SCSI_DCH_U320_MODE */
      hiob->haStat = SCSI_HOST_DETECTED_ERR;

#if SCSI_CRC_NOTIFICATION
      hiob->residualLength = SCSI_PARITY_DATA_PARITY_ERR;
#endif /* SCSI_CRC_NOTIFICATION */

      phase = SCSIhWait4Req(hhcb);
      if (phase == (SCSI_UEXACT8)-1)
      {
         sxfrctl1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), (sxfrctl1 & ~SCSI_ENSPCHK));
         /* don't turn on parity if per iob parity checking. */
         /* we need parity off in bus free                   */
#if !SCSI_PARITY_PER_IOB
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), (sxfrctl1 | SCSI_ENSPCHK));
#endif /* !SCSI_PARITY_PER_IOB */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1),
                       (SCSI_CLRSCSIPERR | SCSI_CLRATNO));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), SCSI_CLRSCSIINT);

         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID13);
         return;
      }

      /* In legacy hardware, the SCSIPERR bit in the SSTAT1 register  */
      /* would not get reset if we set the corresponding bit in the   */
      /* CLRSINT1 register.  In Harpooon, the SCSIPERR bit gets reset */
      /* immediately.  CHIM's code has been changed to not rely on    */
      /* our previous chip behavior.  Moved the clear SCSIPERR code   */
      /* here as it was at before the call to SCSIhWait4Req().        */

      /* Clear Physical error */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRSCSIPERR);

#if !(SCSI_NEGOTIATION_PER_IOB && SCSI_BIOS_SUPPORT)
      targetUnit = SCSI_TARGET_UNIT(hiob);
      /* need to re-negotiate with this target.    */ 
      if (SCSI_hWIDEORSYNCNEGONEEDED(targetUnit->deviceTable))
      {
         SCSIhUpdateNeedNego(hhcb,targetUnit->scsiID);
      }
#endif /* !(SCSI_NEGOTIATION_PER_IOB && SCSI_BIOS_SUPPORT)*/

      /* Turn parity and CRC checking off. */
      /* They will be turned back on in message out phase. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CRCCONTROL),
          (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_CRCCONTROL)) & ~SCSI_CRCVALCHKEN));
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
      sxfrctl1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), (sxfrctl1 & ~SCSI_ENSPCHK));
   }
}

/*********************************************************************
*
*  SCSIhPacketizedPhysicalError
*
*     Handle SCSI physical errors in packetized mode (CRC, DG, DT)
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Assumption that we are in mode 3.
*
*                 This routine handles a CRC error occurrs during
*                 packetized connection:
*                 a) If a CRC error occurred during a LQ packet,
*                    Harpoon will set LQICRCI1 (bit 4 of LQISTAT1
*                    reg) to 1.
*                    It might make sense to disable this interrupt
*                    and use this bit as a pollable status during
*                    SCSIPERR handler.
*                    If the target is following all the rules,
*                    the SCSI bus will be hung with the last ACK of
*                    the LQ packet pending.
*                    If ENAUTOATNP is set Harpoon will already have
*                    asserted ATN.
*                    1) If ENAUTOATNP is zero, write a 1 to ATNO
*                       (bit 4 of SCSISIGO reg) to get ATN asserted
*                       on SCSI bus.
*                    2) Set LQIRETRY (bit 7 of LQCTL2 reg) to release
*                       the last ACK of the LQ.
*                    3) Target SHOULD go to MESSAGE OUT phase.
*                    4) Before sending a message byte to target:
*                       a) clear the outgoing ATN.
*                       b) set LQIRETRY again.
*                       c) clear the NONPACKREQ (bit 5 of CLRSINT2 reg)
*                    5) Send a message byte to target.
*                    6) Wait for bus free.
*                    6) LQIMGR will be ready to process a new LQ
*                       if that is what the target does.
*
*                 b) If a CRC error occurred during a non-LQ packet.
*                    Harpoon will set LQICRCI2 (bit 3 of LQISTAT1 reg)
*                    to 1.
*                    It might make sense to disable this interrupt
*                    and use this bit as a pollable status during
*                    SCSIPERR handler.
*                    The last ACK of the packet is not held back, so
*                    CHIM should already have set ENAUTOATNP on to get
*                    the ATN asserted in time.
*                    1) Target SHOULD be in MESSAGE OUT phase already.
*                    2) Before sending a message byte to target:
*                       a) clear the outgoing ATN.
*                       b) set LQIRETRY (bit 7 of LQCTL2 reg)
*                       c) clear the NONPACKREQ (bit 5 of CLRSINT2 reg)
*                    3) Send a message byte to target.
*                    4) LQIMGR will be ready to process a new LQ
*                       if that is what the target does.
*                  
*********************************************************************/
#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT)
void SCSIhPacketizedPhysicalError (SCSI_HHCB SCSI_HPTR hhcb,
                                   SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UINT32 i = 0;            /* time-out counter */
   SCSI_UEXACT16 scbNumber;
   SCSI_UEXACT8 lqistat1;
   SCSI_UEXACT8 lqistat2;
   SCSI_UEXACT8 lqistate;
   SCSI_UEXACT8 sxfrctl1;
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 sstat3;
   SCSI_UEXACT8 ubyteWork;
   SCSI_UEXACT8 modeSave;

#if !(SCSI_DOWNSHIFT_MODE)
   if ((hiob != SCSI_NULL_HIOB) &&
       hiob->SCSI_IF_freezeOnError &&
       (!(SCSI_TARGET_UNIT(hiob)->targetControl->freezeMap &
            SCSI_FREEZE_HIPRIEXISTS)))
   {
      SCSI_hFREEZEHWQUEUE(hhcb,hiob,SCSI_AE_FREEZEONERROR_START);
   }
#endif /* !(SCSI_DOWNSHIFT_MODE) */

   lqistat1 = OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT1));
   lqistat2 = OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT2));
   sstat3 = OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT3));
#if !SCSI_ASPI_REVA_SUPPORT   
   if ((SCSI_hHARPOON_REV_B(hhcb) || SCSI_hDCH_SCSI_CORE(hhcb))
        &&  (sstat3 & SCSI_TRNERR))
   {

   /* The 7902 Rev B introduced a new status field, 'TRNERR', in the SSTAT3  */
   /* register. This indicates a training error was detected. The details of */
   /* new bit are described in the 7902 rev B addendum. Associated with      */
   /* this bit is an interrupt enable. When ENTRNERR is set a SCSIINT can    */
   /* occur. At this time is was decided not to enable this interrupt. This  */
   /* is because the 7902 does not alter its behavior by asserting attention.*/
   /* Tests forcing the 'trnerr' status always led to a Physical error due   */
   /* to CRC errors. This routine already handles that. Here check the new   */
   /* SSTAT3 field and if necessary clear it. In the future it may become a  */ 
   /* requirement to report this condition to the OSM by possibly adding a   */ 
   /* new EVENT.                                                             */

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT3), SCSI_CLRTRNERR);
   }
#endif /* !SCSI_ASPI_REVA_SUPPORT */
   /* If iuCRC error is not for LQ packet (LQICRCI1), not for non-LQ packet */
   /* (LQICRCI2 or not for unexpected bus phase between packet (LQIPHASE0). */

   /* Harpoon 2 A3 can be in IHALT1 state, which indicates that the machine */
   /* believes that something went wrong before or during the reception of  */
   /* an LQ packet. In the Harpoon2 A3 logic, all the branches into IHALT1  */
   /* set something in LQISTAT1, or sets bit LQIPHASE0 in register LQISTAT2 */
   /* The transition that sets LQIPHASE0 is from ISTART.  ISTART is the     */
   /* first state after a reselect in or after the end of a packet or       */
   /* stream.  If LQIPHASE0 is set, recover by setting LQIRETRY to 1.       */
   if ((!(lqistat1 & SCSI_LQICRCI1)) && (!(lqistat1 & SCSI_LQICRCI2)))
   {
      /* Harpoon2 Rev A has a problem wherein during initiator mode if  */ 
      /* it detects a CRC error during a non-LQ packet (like during CRC */
      /* interval), it would set the SCSIPERR bit immediately, but it   */
      /* would set LQICRCI2 bit only when all the bytes of the packet   */
      /* have been transferred.  The details of this hardware problem   */
      /* can be found in the Razor database entry #535.                 */
      /* The workaround is to check LQISTATE register (0x4E) has a      */
      /* value ">= 0x1E" AND "<= 0x28", then it is an LQICRCI2 error.   */
      /* CHIM will handle it.                                           */

      /* The latest update is to extend the check to "<= 0x29". Harpoon */
      /* can enter this state after detected CRC error and set SCSIPERR */
      /* before LQICRCI2 set.  ASIC will stay in this state until data  */
      /* packet transfer complete.  Then it enters the next state and   */
      /* set LQICRCI2.                                                  */
   
      /* As harpoon should not stay in these states when it detected    */
      /* iuCRC error for non-LQ packet.  In this case, the iuCRC error  */
      /* data packet transfer might hang or the data packet is a large  */
      /* packet and harpoon's fifo gets full because sequencer already  */
      /* paused (hardware finished all preload S/G elements).           */
      /* At this update, CHIM will:                                     */
      /* 1. Try to simulate the BITBUCKET by loading a dummy S/G element*/
      /*    so that the iuCRC error data packet transfer can finish.    */
      /* 2. If the transfer hang, then CHIM will reset SCSI and         */
      /*    reinitialize the chip.                                      */

      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      lqistate = OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTATE));
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

      /* Is the hardware in the middle of ACKing a non-LQ packet? */
      if (SCSI_hHARPOON_REV_A_AND_B(hhcb) &&
          (lqistate >= 0x1E) && (lqistate <= 0x29))
      {
         /* Yes.  Keep feeding dummy S/G elements until the target gets out */
         /* of the DATA phase. */
         if (SCSIhSimulateBitBucket(hhcb))
         {
            /* Data packet transfer complete.                */
            /* Assume we have iuCRC error for non-LQ packet. */
            lqistat1 |= SCSI_LQICRCI2;
         }
         else
         {
            /* Data packet transfer hang.  Reset scsi bus and reinit the chip */
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID45);
            return;
         }
      }

      /* No.  The hardware is not in the middle of ACKing a non-LQ packet.  */
      /* Has it finished ACKing all the bytes of a non-LQ packet and the    */
      /* target has switched out of the DATA phase? (LQIPHASE2 is set if the*/
      /* target switched out of DATA phase after stream packet that is not  */
      /* the last packet of the stream, and LQIPHASE0 is set if the target  */
      /* switched out of DATA phase after a non-stream packet or a last     */
      /* stream packet) */
      else if ((OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT1)) & SCSI_LQIPHASE2) ||
               (OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT2)) & SCSI_LQIPHASE0))
      {
         /* Assume we have iuCRC error for non-LQ packet. But do not clear  */
         /* the LQIPHASE2 interrupt, as it will be done below once we know  */
         /* that the scsi bus has changed to MSG-OUT phase. Also, we do not */
         /* have to clear the LQIPHASE0 bit. */
         lqistat1 |= SCSI_LQICRCI2;
      }

      /* No.  The hardware is neither in the middle nor at the end of a non- */
      /* LQ packet.  CRC error must have been detected during an LQ packet.  */
      /* Just record the fact and proceed to ACK the CRC byte of the LQ      */
      /* packet. */
      else
      {
         lqistat1 |= SCSI_LQICRCI1;
      }
   }

   /* @REVISIT - this is for debug purpose and maybe remove later */
   /* Check other non LQICRCIx statuses, except LQIABORT because  */
   /* ABORTPENDING bit is never set (due to hardware bug) and     */
   /* LQIOVER2 bit is tied to zero.                               */
   switch (lqistat1 & ~(SCSI_LQICRCI1 | SCSI_LQICRCI2))
   {
      case (SCSI_LQIPHASE1):
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID73);
         return;

      case (SCSI_LQIBADLQI):
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID75);
         return;

      case (SCSI_LQIOVERI1):
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID76);
         return;

      default:
         /* Assume we can proceed further with recovery process. */
         break;
   }

   /* For CRC error of an LQ packet (LQICRCI1) */
   if (lqistat1 & SCSI_LQICRCI1)
   {
      /* ATN should be assertd by now. */

      /* Set LQIRETRY to release the last ACK of the LQ.      */
      /* The target should go to MSGOUT phase after last ACK. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQCTL2),SCSI_LQIRETRY);
   }
   else
   {
      if (lqistat1 & SCSI_LQICRCI2)
      {
         modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

         SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);

         /* Turn on the bit bucketting. */
         ubyteWork = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL),
            ubyteWork | SCSI_M01BITBUCKET);
         
         /* The hardware automatically turns it off when the bit bucketting */
         /* is completed. Simply wait for the event. */
         i = 0;
         while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL)) &
                   SCSI_M01BITBUCKET) && (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
         {
            OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
         }

         if (i >= SCSI_REGISTER_TIMEOUT_COUNTER)
         {
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID80);
            return;
         }

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQCTL2),SCSI_LQITOIDLE);

         SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
      }
   }

   /* Must be parity error or CRC */
   sxfrctl1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
         
   /* Turn off parity checking to clear any residual error. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), (sxfrctl1 & ~SCSI_ENSPCHK));

#if !(SCSI_PARITY_PER_IOB)
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), (sxfrctl1 | SCSI_ENSPCHK));
#endif /* !(SCSI_PARITY_PER_IOB) */

   /* Wait for a REQ asserted with valid SCSI phase */
   phase = SCSIhWait4Req(hhcb);

   /* Target should be in MESSAGE OUT phase.             */
   /* If it is not, reset SCSI bus and re-init the chip. */
   if ((phase == (SCSI_UEXACT8)-1) || (phase != SCSI_MOPHASE))
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRLQIINT1),
                    (SCSI_CLRLQICRCI1 | SCSI_CLRLQICRCI2 | SCSI_CLRLQIPHASE1));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1),
                    (SCSI_CLRSCSIPERR | SCSI_CLRATNO));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), SCSI_CLRSCSIINT);

      /* Invalid SCSI phase - reset SCSI bus and re-init the chip */
      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID57);
      return;
   }

   /* Harpoon2 Rev A does not set the LQICRCI2 in the case of an CRC error  */
   /* detected during a data stream packet.  Instead, it sets the LQIPHASE2 */
   /* bit.  Also, the hardware will be in a good state only if a complete   */
   /* stream packet has been ACKed (ie. DLZERO is set) before the scsi phase*/
   /* changes to MSG-OUT.  We look for this condition and handle            */
   /* accordingly. */
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT1)) & SCSI_LQIPHASE2)
   {
      /* Just clear the LQIPHASE2 interrupt. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRLQIINT1), (SCSI_CLRLQIPHASE2));

      /* Make sure that DLZERO status in the mode that detected the CRC error*/
      /* is set. */
      SCSIhSetModeToCurrentFifo(hhcb);
      if (!(OSD_INEXACT8(SCSI_AICREG(SCSI_M01DFFSTAT)) & SCSI_DLZERO))
      {
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID74);
         return;
      }
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

      /* Record the fact that we got CRC error during non-LQ packet. */
      lqistat1 |= SCSI_LQICRCI2;
   }
   else if (OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT2)) & SCSI_LQIPHASE0)
   {
      /* Record the fact that we got CRC error during non-LQ packet. */
      lqistat1 |= SCSI_LQICRCI2;
   }

   if (lqistat1 & SCSI_LQICRCI1)
   {
      /* Did the target take all the LQ packet bytes before switching out */
      /* of DATA phase? */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_OS_SPACE_CNT)) >= (SCSI_UEXACT8)0x20)
      {
         /* Yes, it did.  We can proceed further with the recovery. Put back */
         /* mode to MODE3 for the logic below to continue. */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
      }
      else
      {
         /* No, it did not.  Hardware is stuck.  The only way to recover  */
         /* from it is BadSeq. */
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID77);
         return;
      }
   }

   /* Handle specific task for CRC error of a non-LQ packet (LQICRCI2) */
   if (lqistat1 & SCSI_LQICRCI2)
   {
      /* HIOB should not be NULL for LQICRCI2 status */
      if (hiob == SCSI_NULL_HIOB)
      {
         /* Get active SCB by reading the tag number (byte 2 and 3) in */
         /* the last LQ packet.  This last LQ packet is stored in      */
         /* hardware LQ Packet In registers.                           */
         scbNumber = ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_LQIN02))) << 8) |
                     (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_LQIN03)));

         /* Get active HIOB */
         if (SCSI_IS_NULL_SCB(scbNumber) || 
             ((hiob = SCSI_ACTPTR[scbNumber]) == SCSI_NULL_HIOB))
         {
            sxfrctl1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), (sxfrctl1 & ~SCSI_ENSPCHK));
            /* don't turn on parity if per iob parity checking. */
            /* we need parity off in bus free. */
#if !(SCSI_PARITY_PER_IOB)
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), (sxfrctl1 | SCSI_ENSPCHK));
#endif /* !(SCSI_PARITY_PER_IOB) */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRLQIINT1), SCSI_CLRLQICRCI2);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1),
                          (SCSI_CLRSCSIPERR | SCSI_CLRATNO));
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), SCSI_CLRSCSIINT);

            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID56);
            return;
         }
      }

      /* Clear LQICRCI2 and LQIPHASE1 statuses */
      /* The reason that we need to clear the LQIPHASE1 status is the harpoon */
      /* finishes the packet that had the error and then goes on to expect an */
      /* LQ packet.  It gets a MSGOUT instead, and flags an LQIPHASE1.        */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRLQIINT1),
                    (SCSI_CLRLQICRCI2 | SCSI_CLRLQIPHASE1));

      /* Set haStat that HA detected CRC error */
      hiob->haStat = SCSI_HOST_DETECTED_ERR;
#if SCSI_CRC_NOTIFICATION
      hiob->residualLength = SCSI_PARITY_CRC_ERR;
#endif /* SCSI_CRC_NOTIFICATION */
   }
   else if (lqistat1 & SCSI_LQICRCI1)  /* CRC error on LQ packet */
   {
      /* Clear LQICRCI1 error status */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRLQIINT1), SCSI_CLRLQICRCI1);
   }

   /* If CRC error happened on an LQ packet the target should go to bus     */
   /* free.  If CRC error happened on a non-LQ packet the target may or may */
   /* not go to bus free.  In either case, we can disable the unexpected    */
   /* free interrupt, because the hardware will automatically turn on the   */
   /* ENBUSFREE if the target goes to DATA-IN after we ACK the Initiator    */
   /* detected error message. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE1),
                 OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE1)) & ~SCSI_ENBUSFREE);

   /* Clear Physical error */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRSCSIPERR);

   /* Clear scsi interrupt */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), SCSI_CLRSCSIINT);

   /* Before sending a message byte to SCSI bus: */

   /* Clear the outgoing ATN */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);

   /* Set LQIRETRY to get the harpoon's LQIMGR out of the HALT state */
   /* and expect an LQ packet again.                                 */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQCTL2), SCSI_LQIRETRY);

   /* clear the non-Packetized dataphase REQ */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT2),SCSI_CLRNONPACKREQ);
   
   /* Send "Initiator Detected Error" message byte to target */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL), SCSI_MSG05);

   /* Wait until ACK get dropped */
   while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_ACKI) &&
          (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
   {
      OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
   }

   if (i >= SCSI_REGISTER_TIMEOUT_COUNTER)
   {
      /* ACK did not drop.  The bus must be hang. */
      /* Consider this as a bad SCSI sequence.    */
      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID58);
      return;
   }

   /* Here CHIM will check for 'bus free status' to be set.                */
   /* Also, need to check if ENBUSFREE gets turned ON by the hardware (it  */
   /* will be turned on if we got CRC error on a non-LQ packet and the     */
   /* target switched to DATA IN phase instead of bus free, after we ACKed */
   /* the Initiator Detected Error message.                                */
   /* Also, we need to check if the phase goes to MSG-IN, as some devices  */
   /* can go to MSG-IN (instead of bus free) to send a QAS message and then*/
   /* go bus free.                                                         */
   /* Also, need to check for third party reset if one might occurs. */
   i = 0;
   while (((!(OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & 
             (SCSI_BUSFREE | SCSI_SCSIRSTI))) &&
           !(OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE1)) & SCSI_ENBUSFREE) &&
           (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIPHASE)) != SCSI_MSG_IN)) &&
          (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
   {
      OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
   }

   if (i >= SCSI_REGISTER_TIMEOUT_COUNTER)
   {
      /* Bus did not go to BUS FREE phase. 
       * Consider this as a bad SCSI sequence.
       */
      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID64);
      return;
   }
}
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT) */

/*********************************************************************
*
*  SCSIhSimulateBitBucket
*
*     Simulate a hardware bitbucket to finish data packet transfer
*
*  Return Value:  0 - data packet transfer is not complete
*                 1 - data packet transfer complete
*
*  Parameters:    hhcb
*
*  Remarks:       This routine simulates the hardware BITBUCKET by
*                 loading a dummy S/G element so that the a data
*                 packet transfer can finish.
*                 Bitbuket to continue data packet transfer until
*                 either:
*                 1. Hardware set LQICRCI2 which indicates transfer
*                    finish, or
*                 2. The scsi phase is MSG-OUT phase means LQICRCI2
*                    not set but the data packet transfer complete, or
*                 3. Time-out counter has been elasped means transfer
*                    hang.
*
*********************************************************************/
#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT)
SCSI_UEXACT8 SCSIhSimulateBitBucket (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS segAddress;
   SCSI_UINT32 i = 0;            /* time-out counter */
   SCSI_UEXACT8 savedMode;
   SCSI_UEXACT8 sxfrctl1;
   SCSI_UEXACT8 crccontrol;

   /* Save the mode register */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Turn off parity and CRC checking */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   crccontrol = OSD_INEXACT8(SCSI_AICREG(SCSI_CRCCONTROL));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CRCCONTROL), (crccontrol & ~SCSI_CRCVALCHKEN));
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   sxfrctl1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), (sxfrctl1 & ~SCSI_ENSPCHK));
         
   /* We use the SPI status IU buffer.           */
   /* Get bus address of the dummy S/G segment . */
#if SCSI_ASPI_SUPPORT
   OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,SCSI_SPI_STATUS_PTR(hhcb));
   segAddress = hhcb->busAddress;
#else
   segAddress = OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,SCSI_SPI_STATUS_PTR(hhcb));
#endif /* SCSI_ASPI_SUPPORT */
   /* Adjust SPI status IU buffer if current mode is 1 */
   SCSIhSetModeToCurrentFifo(hhcb);
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_DFFSTAT)) & SCSI_CURRFIFO)
   {
      /* SPI status IU buffer can hold 2 IU status, one from each fifo. */
      /* The 2nd half of SPI status buffer uses for data channel 1.     */
      OSD_ADJUST_BUS_ADDRESS(hhcb,&segAddress,SCSI_MAX_STATUS_IU_SIZE);
   }

   /* Bitbuket to continue data packet transfer until either:       */
   /* 1. Hardware set LQICRCI2, LQIPHASE2, or LQIPHASE0 which       */
   /*    indicates transfer finish, or                              */
   /* 2. The scsi phase is MSG-OUT phase means LQICRCI2 not set but */
   /*    the data packet transfer complete, or                      */
   /* 3. Time-out counter has been elasped means transfer hang.     */
   while ((!(OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT1)) & SCSI_LQICRCI2)) &&
          (!(OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT1)) & SCSI_LQIPHASE2)) &&
          (!(OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT2)) & SCSI_LQIPHASE0)) &&
          ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_BUSPHASE) != SCSI_MOPHASE) &&
          (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
   {
      /* Preload level is available? */
#if !SCSI_DCH_U320_MODE      
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_DFSTATUS)) & SCSI_PKT_PRELOAD_AVAIL)
      {
#else      
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_DFSTATUS)) & SCSI_PRELOAD_AVAIL)
      {
#endif /* !SCSI_DCH_U320_MODE */
         /* Load dummy S/G segment address to host address */
#if (OSD_BUS_ADDRESS_SIZE == 64)
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR0),
                       ((SCSI_OCTLET SCSI_SPTR)&segAddress)->u8.order0);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR1),
                       ((SCSI_OCTLET SCSI_SPTR)&segAddress)->u8.order1);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR2),
                       ((SCSI_OCTLET SCSI_SPTR)&segAddress)->u8.order2);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR3),
                       ((SCSI_OCTLET SCSI_SPTR)&segAddress)->u8.order3);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR4),
                       ((SCSI_OCTLET SCSI_SPTR)&segAddress)->u8.order4);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR5),
                       ((SCSI_OCTLET SCSI_SPTR)&segAddress)->u8.order5);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR6),
                       ((SCSI_OCTLET SCSI_SPTR)&segAddress)->u8.order6);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR7),
                       ((SCSI_OCTLET SCSI_SPTR)&segAddress)->u8.order7);
#else /* (OSD_BUS_ADDRESS_SIZE != 64) */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR0),
                       ((SCSI_QUADLET SCSI_SPTR)&segAddress)->u8.order0);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR1),
                       ((SCSI_QUADLET SCSI_SPTR)&segAddress)->u8.order1);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR2),
                       ((SCSI_QUADLET SCSI_SPTR)&segAddress)->u8.order2);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHADDR3),
                       ((SCSI_QUADLET SCSI_SPTR)&segAddress)->u8.order3);
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */
         /* Load dummy S/G segment length to host count */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHCNT0),
                       (SCSI_UEXACT8)(SCSI_MAX_STATUS_IU_SIZE & 0x00FF));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHCNT1),
                       (SCSI_UEXACT8)((SCSI_MAX_STATUS_IU_SIZE & 0xFF00) >> 8));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHHCNT2),(SCSI_UEXACT8)0x00);

         /* Enable drop from preload to active.  Keep data packet moving. */
#if !SCSI_DCH_U320_MODE
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFCNTRL),
                       (SCSI_PRELOADEN | SCSI_SCSIEN | SCSI_HDMAEN));
#else                       
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFCNTRL),
                       (SCSI_PRELOADEN | SCSI_SETSCSIEN | SCSI_SETHDMAEN));
#endif /* !SCSI_DCH_U320_MODE */
      }
   }
  
   /* Restore parity and CRC checking state */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CRCCONTROL), crccontrol);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), sxfrctl1);

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);

   if (i >= SCSI_REGISTER_TIMEOUT_COUNTER)
   {
      return(0);
   }
   else
   {
      return(1);
   }
}
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT) */

/*********************************************************************
*
*  SCSIhLQIAbortOrLQIBadLQI
*
*     Handle LQIBADLQI and LQIABORT error status from the LQI manager
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:                
*                  
*********************************************************************/
#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT && !SCSI_ASPI_REVA_SUPPORT)
void SCSIhLQIAbortOrLQIBadLQI (SCSI_HHCB SCSI_HPTR hhcb,
                               SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
/* REVISIT - The code will be tested when hardware fix ABORTPENDING issue */
#if 0
   SCSI_HIOB SCSI_IPTR hiobSelecting;
   SCSI_UEXACT16 waitingScb;
   SCSI_UEXACT8 scsiseq;
#endif /* 0 */
   SCSI_UEXACT8 phase;
   SCSI_UINT32 i;       /* time out counter */
            
   if (hiob == SCSI_NULL_HIOB) 
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRLQIINT1),
          (SCSI_CLRLQIABORT | SCSI_CLRLQIBADLQI));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), SCSI_CLRSCSIINT);

      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID69);
      return;
   }

   /* Raise atn signal to issue abort message to target */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO),SCSI_ATNO);

   /* Hardware stopped at the last ACK of LQ packet (i.e the iu CRC bytes). */
   /* Let the hardware finish the last ACK of LQ packet by writing 1 to     */
   /* LQIRETRY bit.                                                         */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQCTL2),SCSI_LQIRETRY);

   phase = SCSIhWait4Req(hhcb);

   /* Target should enter message-out phase */
   if (phase != SCSI_MOPHASE)
   {
      /* If the target does not switch to message-out phase */
      /* then perform full error recovery.                  */
      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID68);
      return;
   }

/* REVISIT - The code will be tested when hardware fix ABORTPENDING issue */
#if 0
   /* For LQIABORT interrupt, hardware must detect a reselected SCB has   */
   /* been aborted by CHIM.  Since the aborted SCB is queued by CHIM into */
   /* target execution queue at the time of active abort handling.        */
   /* CHIM will search target execution queue, remove the active aborted  */
   /* SCB and mend the execution queue and target execution queue.        */
   /* If seletion has been fired for this target, CHIM will disable it.   */
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT1)) & SCSI_LQIABORT)
   {
      /* If selection is currently on */
      if ((scsiseq = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0))) & SCSI_ENSELO)
      {
         waitingScb =
            ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
            (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));

         if (!SCSI_IS_NULL_SCB(waitingScb))
         {
            /* Get the HIOB that was attempting to perform selection and
            * check if its target ID matches with the aborted HIOB.
            * If it does then we need to disable selection to ensure
            * that the proper selection setup can be started later by
            * sequencer when CHIM removed the aborted.
            */
            hiobSelecting = SCSI_ACTPTR[waitingScb];

            if ((SCSI_TARGET_UNIT(hiobSelecting)->hhcb == hhcb) &&
                (SCSI_TARGET_UNIT(hiobSelecting)->scsiID == SCSI_TARGET_UNIT(hiob)->scsiID))
            {
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ0),
                             (scsiseq & ~(SCSI_TEMODEO+SCSI_ENSELO)));
               /* To flush the cache */
               scsiseq = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0));
            }
         }
      }

      /* Remove active aborted SCB and mend the execution queue */
      /* and target execution queue.                            */
      SCSI_hUPDATEEXEQ(hhcb,hiob);
   }
#endif /* 0 */

   /* Clear the atn signal */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQCTL2),SCSI_LQIRETRY);

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT2),SCSI_CLRNONPACKREQ);
                
   /* Abort the I/O by sending the relevant ABORT message.   */
   /* Disable the 'Unexpected bus free' interrupt, as we are */
   /* going to expect one, after sending the abort message.  */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE1),
                 (OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE1)) & ~SCSI_ENBUSFREE));

   /* Send abort message to target */
   if ((hiob->SCSI_IF_tagEnable) && (!hiob->SCSI_IF_disallowDisconnect))
   {
      /* Send ABORT TASK message */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL),SCSI_MSG0D);
   }
   else
   {
      /* Send ABORT TASK SET message */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL),SCSI_MSG06);
   }

   /* Need to check for 'bus free status' to be set. */
   /* Also, need to check for third party resets.    */
   i = 0;
   while ((!(OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & 
             (SCSI_BUSFREE | SCSI_SCSIRSTI))) &&
          (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
   {
      OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
   }

   if (i >= SCSI_REGISTER_TIMEOUT_COUNTER)
   {
      /* Bus did not go to BUS FREE phase. 
       * Consider this as a bad SCSI sequence.
       */
      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID65);
      return;
   }

/* REVISIT - The code will be tested when hardware fix the issue in razor # */
#if 0
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_LQISTAT1)) & SCSI_LQIABORT)
   {
      /* Terminate the I/O with an appropriate error. */
      hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE;
      SCSIhTerminateCommand(hiob);

      /* REVISIT - Need to clear hardware ABORTPENDING bit when all */
      /* aborted HIOBs are done. */
      /* Currently the feature is off because hardware does not work */
   }
#endif /* 0 */
}
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT && !SCSI_ASPI_REVA_SUPPORT) */

/*********************************************************************
*
*  SCSIhLQOErrors
*
*     Handle SCSI_LQOPHACHG1 exception from the LQO manager
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:   Assumes mode 3 on entry             
*                  
*********************************************************************/
#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT && !SCSI_ASPI_REVA_SUPPORT)
void SCSIhLQOErrors (SCSI_HHCB SCSI_HPTR hhcb,
                     SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 message;

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQCTL2), SCSI_LQORETRY);
            
   if (hiob == SCSI_NULL_HIOB) 
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), SCSI_CLRSCSIINT);
      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID70);
      return;
   }

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO),SCSI_ATNO);

   phase = SCSIhWait4Req(hhcb);
   if (phase != SCSI_MOPHASE)
   {
      /* If the target does not switch to message out phase
       * then perform full error recovery.
       */
       SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID71);
       return;
   }

   /* need to send abort task message to target */
   if ((hiob->SCSI_IF_tagEnable) && (!hiob->SCSI_IF_disallowDisconnect))
   {
      /* Abort tagged command */
      message = SCSI_MSG0D;
   }
   else
   {
      /* Abort non-tagged command */
      message = SCSI_MSG06;
   }

   /* clear the atn signal */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL),message);
}
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT && !SCSI_ASPI_REVA_SUPPORT) */

/*********************************************************************
*
*  SCSIhGetPcixStatus
*
*     Read Split Status registers 96h and 9Eh
*
*  Return Value:  status in Split Status registers
*
*  Parameters:    hhcb
*
*  Remarks:
*
*********************************************************************/
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
SCSI_UEXACT8 SCSIhGetPcixStatus(SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 i;
   SCSI_UEXACT8 mode,modeSave;
   SCSI_UEXACT8 discardStatus,pcixStatus;

   /* 
     Read, save and clear if necessary fields in reg 96h and 9Eh.
     These errors should be directly associated with any SPLTINT
     status.
    */
    
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   for (pcixStatus=0,i=0;i<4;i++)
   {
      /* Set mode  */
      mode = (i << 4) + i;
      
      SCSI_hSETMODEPTR(hhcb,scsiRegister,mode);
      /* reg 96h */
      if (discardStatus = OSD_INEXACT8(SCSI_AICREG(SCSI_DCH0SPLTSTAT0)))
      {
         pcixStatus |= discardStatus;
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCH0SPLTSTAT0), discardStatus);
      }   
   }

   /* Set to mode 0 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE0);
   /* read register 9E */
   if (discardStatus = OSD_INEXACT8(SCSI_AICREG(SCSI_SG01SPLTSTAT0)))
   {
      pcixStatus |= discardStatus;
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SG01SPLTSTAT0), discardStatus);
   }

   /* Set to mode 1 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE1);
   /* read register 9E */
   if (discardStatus = OSD_INEXACT8(SCSI_AICREG(SCSI_SG01SPLTSTAT0)))
   {
      pcixStatus |= discardStatus;
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SG01SPLTSTAT0), discardStatus);
   }
   
   /* Restore mode to original mode on entry. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
   
   return (pcixStatus);
}
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */

/***************************************************************************
*
*  SCSIhClearPCIorPCIXError
*
*     PCI or PCI-X errors can occur at anytime. This routine does not
*     determine the root cause but simply checks for an error and if 
*     detected clears all of the status registers including the ones
*     located within the confines of the PCI Config Space. In the case
*     that a PCI or PCI-X error cannot be successfully cleared status
*     returned indicates a unrecoverable condition exist.
*
*  Return Value:  SCSI_SUCCESS - undetected or successfully cleared
*                 SCSI_FAILUE - detected but unsuccessfully cleared
* 
*  Parameters:    hhcb
*
*  Remarks:       none
*
***************************************************************************/
#if !SCSI_DCH_U320_MODE
SCSI_UEXACT8 SCSIhClearPCIorPCIXError(SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 modeSave,mode;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 hstintstat,configErrorstatus;
   SCSI_UEXACT8 i;
   SCSI_UEXACT8 dfcntrl,ccsgctl,ccscbctl;
   SCSI_UINT32 j;
   
   /* Save the HCNTRL value. */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* pause the chip if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }
   /* Save original mode */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* get HSTINTSTAT */
   hstintstat = OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT));
   
   if (hstintstat & SCSI_PCIINT)
   {  
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      
      configErrorstatus = (SCSI_UEXACT8)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_STATUS_HI_REG));
      
      /* A0 - DFF0 */
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_PCISTATDF0)) & (SCSI_PCI_RCVDMABRT | SCSI_PCI_RCVDTABRT))
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE0);
         if ((dfcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL))) & SCSI_HDMAENACK)
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFCNTRL), dfcntrl & ~SCSI_HDMAEN);
            j = 0;
            while ((OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL)) & SCSI_HDMAENACK) &&
                   (j++ < (SCSI_UINT32)SCSI_REGISTER_TIMEOUT_COUNTER))
            {
               OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
            }
         }
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      }

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCISTATDF0), 0xFF);

      /* A1 - DFF1 */
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_PCISTATDF1)) & (SCSI_PCI_RCVDMABRT | SCSI_PCI_RCVDTABRT))
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE1);
         if ((dfcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL))) & SCSI_HDMAENACK)
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFCNTRL), dfcntrl & ~SCSI_HDMAEN);
            j = 0;
            while ((OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL)) & SCSI_HDMAENACK) &&
                   (j++ < (SCSI_UINT32)SCSI_REGISTER_TIMEOUT_COUNTER))
            {
               OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
            }
         }
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      }

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCISTATDF1), 0xFF);

      /* A2 - SG01 */   
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_PCISTATSG)) & (SCSI_PCI_RCVDMABRT | SCSI_PCI_RCVDTABRT))
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE0);
         if ((ccsgctl = OSD_INEXACT8(SCSI_AICREG(SCSI_CCSGCTL))) & SCSI_CCSGENACK)
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSGCTL), ccsgctl & ~SCSI_CCSGEN);
            j = 0;
            while ((OSD_INEXACT8(SCSI_AICREG(SCSI_CCSGCTL)) & SCSI_CCSGENACK) &&
                   (j++ < (SCSI_UINT32)SCSI_REGISTER_TIMEOUT_COUNTER))
            {
               OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
            }
         }

         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE1);
         if ((ccsgctl = OSD_INEXACT8(SCSI_AICREG(SCSI_CCSGCTL))) & SCSI_CCSGENACK)
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSGCTL), ccsgctl & ~SCSI_CCSGEN);
            j = 0;
            while ((OSD_INEXACT8(SCSI_AICREG(SCSI_CCSGCTL)) & SCSI_CCSGENACK) &&
                   (j++ < (SCSI_UINT32)SCSI_REGISTER_TIMEOUT_COUNTER))
            {
               OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
            }
         }

         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      }

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCISTATSG), 0xFF);

      /* A3 - CMC */
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_PCISTATCMC)) & (SCSI_PCI_RCVDMABRT | SCSI_PCI_RCVDTABRT))
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);
         if ((ccscbctl = OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL))) & SCSI_CCSCBENACK)
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSCBCTL), ccscbctl & ~SCSI_CCSCBEN);
            j = 0;
            while ((OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL)) & SCSI_CCSCBENACK) &&
                   (j++ < (SCSI_UINT32)SCSI_REGISTER_TIMEOUT_COUNTER))
            {
               OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
            }
         }
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      }

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCISTATCMC), 0xFF);

      /* A4 - OVLY */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCISTATOVLY), 0xFF);

      /* A6 - MSI */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCISTATMSI), 0xFF);

      /* A7 - TARG */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCISTATTARG), 0xFF);
      
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_STATUS_HI_REG), configErrorstatus);
   }
   /* Since STAETREM bit in spltstat0 register triggers PCI interrupt, */
   /* the logic needs to check either PCI or Split interrupt           */
   else if (hstintstat & (SCSI_PCIINT | SCSI_SPLTINT))
   {
      for (i=0;i<4;i++)
      {
         /* Set mode  */
         mode = (i << 4) + i;
         SCSI_hSETMODEPTR(hhcb,scsiRegister,mode);

         /* register 96h */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCH0SPLTSTAT0), 0xff);
      }

      /* Set to mode 0 */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE0);
      /* register 9Eh */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SG01SPLTSTAT0), 0xff);

      /* Set to mode 1 */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE1);
      /* register 9Eh */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SG01SPLTSTAT0), 0xff);
     
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      
      /* reg 19Bh */
      if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_PCIX_STATUS3)) & SCSI_RSCEM)
      {             
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_PCIX_STATUS3), SCSI_RSCEM);
      }                               

      /* reg 19Ah */
      if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_PCIX_STATUS2)) & SCSI_UNEXPSC)
      {             
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_PCIX_STATUS2), SCSI_UNEXPSC);
      }  

      /* reg 19Dh */
      if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_SLVSPLTSTAT)) & SCSI_PCIX_SLAVE_ERRORS)
      {             
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_SLVSPLTSTAT), SCSI_PCIX_SLAVE_ERRORS);
      }                               

      /* clear split interrupt */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRSPLTINT);
   }
   
   /* check if cleared */
   hstintstat = OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT));
   if (hstintstat & (SCSI_PCIINT | SCSI_SPLTINT)) 
   {
     return (SCSI_FAILURE);    /* Problem was not resolved */
   }   
   
   /* Restore original mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
     
   /* Restore HCNTRL if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
   
   return (SCSI_SUCCESS);
}
#endif /* !SCSI_DCH_U320_MODE */

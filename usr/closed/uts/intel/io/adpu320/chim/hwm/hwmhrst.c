/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*$Header: /vobs/u320chim/src/chim/hwm/hwmhrst.c   /main/178   Sun Jun 22 21:22:41 2003   spal3094 $*/

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
*  Module Name:   HWMHRST.C
*
*  Description:
*                 Codes to implement hardware reset module for hardware
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
*  SCSIhResetChannelHardware
*
*     Reset channel harwdare
*
*  Return Value:  none
*
*  Parameters:    hhcb
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition.
*                 No mode assumptions. Presumably the original mode
*                 is not important as we are initializing the hardware.
*
*********************************************************************/
void SCSIhResetChannelHardware (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UINT32 i;                /* time out counter */
   SCSI_UEXACT8 modeSave;
#if !SCSI_BIOS_SUPPORT
   SCSI_UEXACT8 optionMode;
   SCSI_UEXACT8 hcntrl;
#if !SCSI_DCH_U320_MODE
   SCSI_UEXACT8 commandLow, commandHigh;
#endif /* !SCSI_DCH_U320_MODE */
#endif /* !SCSI_BIOS_SUPPORT */
#if !SCSI_PARITY_PER_IOB
   SCSI_UEXACT8 ubyteWork;
#endif /* !SCSI_PARITY_PER_IOB */

   /* Initializes the currentMode. */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   if (modeSave != SCSI_MODE3)
   {
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   }
   
#if SCSI_TRIDENT_WORKAROUND
   /* Do not remove this code until we verify that it's not
    * required for U320 ASIC.
    */

   /* Initialize SCSISIG to 0 in case the hardware is stuck in
    * selection out which has been seen to occur after a SCSI
    * bus reset. This is due to errors in the hardware selection
    * logic when aborting a selection due to a bus reset. This
    * SEL assertion only occurs on Trident devices. 
    */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), 0x00);
   
#endif /* SCSI_TRIDENT_WORKAROUND */

#if SCSI_PACKETIZED_IO_SUPPORT
   /* Clear initiator's LQI and LQO interrupt statuses */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRLQIINT1), 0xff);

   /* Harpoon2 Rev A3 has a problem that self-clearing feature does not   */
   /* work for all the bits in CLRLQOINT1 register.  The details of this  */
   /* hardware problem can be found in the Razor database entry #663.     */
   /* To workaround this problem the software needs to do an extra write  */
   /* with value 0 to the corresponding bits after writing the bits to    */
   /* clear the interrupts in the LQOSTAT1 register.                      */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRLQOINT1), 0xff);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRLQOINT1), 0x00);

#if (SCSI_TARGET_OPERATION && SCSI_PACKETIZED_TARGET_MODE_SUPPORT)
   if (hhcb->SCSI_HF_targetMode && (SCSI_hHARPOON_REV_1B(hhcb)))
   {
      /* Clear packetized target mode LQI and LQO interrupt statuses */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRLQIINT0), 0xff);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRLQOINT0), 0xff);
   }
#endif /* (SCSI_TARGET_OPERATION && SCSI_PACKETIZED_TARGET_MODE_SUPPORT) */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT0), 0xff);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), 0xff);

   /* Clear host interrupts */
#if !SCSI_DCH_U320_MODE
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), 
                 (SCSI_CLRBRKADRINT | SCSI_CLRSWTMINT |
                  SCSI_CLRSCSIINT | SCSI_CLRARPINT |
                  SCSI_CLRSPLTINT | SCSI_CLRCMDINT)); 
#else
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), 
                 (SCSI_CLRBRKADRINT | SCSI_CLRSWERRINT |
                  SCSI_CLRSCSIINT | SCSI_CLRARPINT)); 
                  
#endif /* !SCSI_DCH_U320_MODE */

#if !SCSI_BIOS_SUPPORT
   /* Indicate to BackEndISR that hstintstat has been cleared
    * so that the local copy in BackEndISR may also be cleared.
    */
   hhcb->SCSI_HP_hstIntsCleared = 1;
#endif /* !SCSI_BIOS_SUPPORT */

   /* Make sure the bit bucket is turned off in case we are recovering from */
   /* data phase hang situation (especially during domain validation) */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1),
                 OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1)) &
                 ~(SCSI_BITBUCKET | SCSI_STIMESEL1 | SCSI_STIMESEL0));

   SCSIhSetSeltoDelay(hhcb);   

   i = 0;
   /* Disable any enabled data channels */
   if (SCSIhDisableDataChannel(hhcb,(SCSI_UEXACT8)SCSI_MODE0))
   {
      /* Force ASIC reset path if channel not disabled. */
      i = SCSI_REGISTER_TIMEOUT_COUNTER;
   }

   if (SCSIhDisableDataChannel(hhcb,(SCSI_UEXACT8)SCSI_MODE1))
   {
      /* Force ASIC reset path if channel not disabled. */
      i = SCSI_REGISTER_TIMEOUT_COUNTER;
   }

   /* This is a workaround for Razor issue #474.
    *
    * Problem:
    *    The DFF0_ACTIVE and DFF1_ACTIVE bits in the DFFMGR's
    *    lqmgr_request_service block are cleared by incoming SCSI
    *    reset but not by outgoing SCSI reset. A simulation test
    *    asserted SCSI reset while the LQIMGR was using a FIFO, so
    *    the ACTIVE bit was set. The ACTIVE bit persisted across
    *    the reset and into the next connection. The LQIMGR fired
    *    up, received an LQ packet, saw the ACTIVE, and did a
    *    close_fifo. This caused a clear_scsien, which halted the
    *    new connection in its tracks.
    *
    * Workaround:
    *    The software workaround is to do a CHIPRST to get out of
    *    this situation, and reinitialize all the hardware registers
    *    and restart all over again. The CHIM cannot determine if
    *    the ASIC is in this state and therefore has to assume
    *    that it might have been and reset the ASIC on any HIM
    *    issued SCSI bus reset. 
    *
    *    The HIM always sets the SCSI_HF_resetSCSI flag if a 
    *    SCSI bus reset was performed prior to calling this
    *    routine. Solution combined with workaround below for
    *    Razor database entry #465.
    */

   /* Did we time-out on waiting for DMA to finish? */

#if !SCSI_BIOS_SUPPORT
   if ((i >= SCSI_REGISTER_TIMEOUT_COUNTER) ||
       (SCSI_hHARPOON_REV_A_AND_B(hhcb) && hhcb->SCSI_HF_resetSCSI))
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

         /* Set Mode 4 - to access PCI Configuration registers */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

         commandLow = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_COMMAND_LO_REG));
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_COMMAND_LO_REG),
                            (commandLow & ~SCSI_PCI_PERRESPEN));
         commandHigh = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_COMMAND_HI_REG));
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_COMMAND_HI_REG),
                            (commandHigh & ~SCSI_PCI_SERRESPEN));
      }
#endif /* !SCSI_DCH_U320_MODE */

      /* Save the HCNTRL value. */
      hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

      /* Reset the chip */
#if !SCSI_DCH_U320_MODE      
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_HCNTRL), SCSI_CHIPRST);

#else      
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_HCNTRL), SCSI_DCHRST);
      /* may need additional work $$ DCH $$ */       
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_HCNTRL), SCSI_PAUSE);
    
#endif /* !SCSI_DCH_U320_MODE */
      /* After chip reset, Harpoon is back to default mode which in Mode 33   */
      /* hhcb->SCSI_HP_currentMode needs to be updated to reflect this.       */
      hhcb->SCSI_HP_currentMode = SCSI_MODE3;

#if !SCSI_DCH_U320_MODE
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

         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_COMMAND_LO_REG), commandLow);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_COMMAND_HI_REG), commandHigh);
      }
#endif /* !SCSI_DCH_U320_MODE */

      /* Set Mode 4 */
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
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_OPTIONMODE),
                    (optionMode | (SCSI_AUTOACKEN | SCSI_BUSFREEREV)));

      hhcb->SCSI_HF_chipReset = 1;

      /* Initialize the hardware */
      SCSIhInitializeHardware(hhcb);

      /* Clear chipReset copy in HHCB as well */
      hhcb->SCSI_HF_chipReset = 0;

      /* Clear chipInitialized flag to indicate that we need reset */
      /* auto rate table if scsi event occurred during run-time.   */
      hhcb->SCSI_HF_chipInitialized = 0;
      /* Wait for bus stable requires mode 3 */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
      SCSI_hWAITFORBUSSTABLE(hhcb);

      /* Restore HCNTRL */
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
#endif /* !SCSI_BIOS_SUPPORT */

#if !SCSI_PARITY_PER_IOB
   /* This is to handle the case that CHIM handles parity error first */
   /* and disable SCSI_ENSPCHK bit to turn parity checking off while  */
   /* sequencer detects invalid phase that sequencer generates        */
   /* phase_error interrupt instead of handle_msg_out interrupt.      */
   /* This results as host adapter's scsi parity checking feature is  */
   /* disabled as it should not be.  The reason is SCSIhHandleMsgOut  */
   /* routine is not called to turn SCSI_ENSPCHK bit back on.         */
   /* The following logic is added to handle this situation.          */
   if (hhcb->SCSI_HF_scsiParity)
   {
      /* Set to Mode 3 */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
      ubyteWork = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
      if (!(ubyteWork & SCSI_ENSPCHK))
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), ubyteWork | SCSI_ENSPCHK);
      }
   }
#endif /* !SCSI_PARITY_PER_IOB */

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ0), 00);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ1), (SCSI_ENRSELI+SCSI_ENAUTOATNP));

   /* Harpoon2 Rev A1 has a problem wherein the receiver DC bias cancellation */
   /* circuit is causing data receive problems.  The workaround is to disable */
   /* the automatic bias cancel logic.  The details of this hardware problem  */
   /* can be found in the Razor database entry #558. */

   /* Here we are setting RCVROFFSTDIS (bit 2) and XMITOFFSTDIS (bit 1) in    */
   /* the DSPDATACTL register (0xC1) to turn off receiver DC compensation and */
   /* the auto tuned transmit cancel. */
   if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
   {
      /* Set Mode 4 */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DSPDATACTL),
          (OSD_INEXACT8(SCSI_AICREG(SCSI_DSPDATACTL)) |
                                    (SCSI_RCVROFFSTDIS | SCSI_XMITOFFSTDIS)));
   }

   /* If the chip has been previously initialized, the hardware rate table */
   /* (NEGODAT registers) will have the valid rate value.  Therefore, we   */
   /* should not reset the rate parameters. */
   if (!hhcb->SCSI_HF_chipInitialized)
   {
      /* reset scsi rate/offset */
      SCSI_hRESETNEGODATA(hhcb);
   }

   /* Reinitialize some data channel specific registers.
    * Mode 0 and Mode 1.
    */
   for (i = 0; i < 2; i++)
   {   
      /* Set to mode 0 or 1. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,(i | (i << 4)));

      /* Set ARP interrupt and Mask for Mode 0/1 */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_M01CLRARPINT),
                    (SCSI_CLRCTXTDONE  |
                     SCSI_CLRSAVEPTRS  |
                     SCSI_CLRCFG4DATA  |
                     SCSI_CLRCFG4ISTAT |
                     SCSI_CLRCFG4TSTAT |
                     SCSI_CLRCFG4ICMD  |
                     SCSI_CLRCFG4TCMD));
   
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_M01ARPMSK),
                    (SCSI_SAVEPTRSMSK  |
                     SCSI_CFG4DATAMSK  |
                     SCSI_CFG4ISTATMSK |
                     SCSI_CFG4TSTATMSK |
                     SCSI_CFG4ICMDMSK  |
                     SCSI_CFG4TCMDMSK));  

      /* 2nd parameter of this function is the mode pointer value */
      SCSIhResetDataChannel(hhcb,(SCSI_UEXACT8)(i | (i << 4)));
   }

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE1),
                 (SCSI_ENSCSIPERR + SCSI_ENSELTIMO + SCSI_ENSCSIRST));

   SCSIhEnableIOErr(hhcb);

   SCSIhResetCCControl(hhcb);

   SCSIhInitCCHostAddress(hhcb);
   SCSI_hRESETBTA(hhcb);

#if SCSI_DCH_U320_MODE

/* ATTN: this could be conditionalized if problems or usage is not desired. */
      
   /* Set the rewind default for DCH to 176 */

   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE0);

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_M0DMACTL), 
      (OSD_INEXACT8(SCSI_AICREG(SCSI_M0DMACTL)) & ~(SCSI_M0STHRESH)) | 0xA0);
   
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE1);

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_M1DMACTL), 
      (OSD_INEXACT8(SCSI_AICREG(SCSI_M1DMACTL)) & ~(SCSI_M1STHRESH)) | 0xA0);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   /* Must initialize these new fields for fully automatic */
   /* SG preload which is supported by the latest ARP code */
   
   OSD_OUTEXACT16(SCSI_AICREG(SCSI_SGMODE), (SCSI_AUTOPRELOAD |
                              SCSI_AUTOINDEX | SCSI_SGBUFSIZE |
                              SCSI_SGAUTOSUB | SCSI_SGAUTOFETCH ));

#else /* !SCSI_DCH_U320_MODE */

   /* Harpoon2 does not set SCSI_SPLTSTADIS bit in PCIXCTL register to be 1   */
   /* as init value.  This causes the chip to issue Target-abort cycle in     */
   /* some conditions.  The correct behavior is not to generate Target-abort  */
   /* when those condition happen.  The details of this hardware issue can    */
   /* be found in the Razor database entry #637.                              */
   /* CHIM code turns on SCSI_SPLTSTADIS bit to eliminate this Target-abort.  */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PCIXCTL),
      OSD_INEXACT8(SCSI_AICREG(SCSI_PCIXCTL)) | SCSI_SPLTSTADIS);

   /* PCIERRGEN register in PCI config space is used to generate or signal    */
   /* PCI errors for testing parity conditions with diagnostic support.       */
   /* When PCIERRGENDIS bit is set in this register, it disables the other    */
   /* bits in this register, thereby preventing them from generating an error */
   /* when active.  PCIERRGENDIS bit should be set to 1 in normal operations. */
   /* However, Harpoon2 Rev A and Rev B chip do not set PCIERRGENDIS bit to   */
   /* be 1 as init value.                                                     */
   /* CHIM code turns on PCIERRGENDIS bit in PCIERRGEN register.              */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_PCIERRGEN_REG), (SCSI_UEXACT8)
          (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_CONFIG_BASE+SCSI_PCIERRGEN_REG)) | SCSI_PCIERRGENDIS));
#endif /* !SCSI_DCH_U320_MODE */

#if SCSI_STANDARD_ENHANCED_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQINTCTL), 0x00);
   }
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */
#if !SCSI_ASPI_REVA_SUPPORT
   if ((SCSI_hHARPOON_REV_B(hhcb)) || SCSI_hDCH_SCSI_CORE(hhcb))
   {
      /* Harpoon2 Rev B has a problem wherein it detects spurious overrun    */
      /* during the LQ/CMD packet delivery.  The reason for this wrong       */
      /* detection is not known, but the following workaround was added to   */
      /* continue further debugging of the ASIC.  The workaround is to turn  */
      /* off the overrun detection feature. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQOSCSCTL),(SCSI_UEXACT8)SCSI_LQONOCHKOVR);

#if SCSI_STANDARD_U320_MODE
      /* Since the definition of LQOSTOP0 has changed for Harpoon2 Rev B     */
      /* ASIC, the STANDARD_U320 mode sequencer will not run in Rev B.  In   */
      /* Rev A, the LQOSTOP0 bit was cleared when LQOBUSFREE1 interrupt is   */
      /* generated by the ASIC, but the bit is left ON in the case of Rev B. */
      /* This breaks 'select_done' logic in the STANDARD_U320 mode sequencer.*/
      /* To make the logic work, we need to put the Harpoon2 Rev B ASIC in   */
      /* the backward compatible mode. */
      if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320)
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_LQOSCSCTL),
            (OSD_INEXACT8(SCSI_AICREG(SCSI_LQOSCSCTL)) |
            (SCSI_UEXACT8)SCSI_H2AVERSION));
      }
#endif /* SCSI_STANDARD_U320_MODE */

#if !SCSI_DCH_U320_MODE
/* Can't enabled this for DCH mode until DCH sequencer is updated. */
      if (SCSI_hHARPOON_REV_B(hhcb))
      {
         /* Harpoon2 Rev B has a problem with "Shadow valid stretch feature".
          * The SHVALIDSTDIS bit must set to 1 to disable the feature. See H1B
          * Razor issue #38 for more detailed information.
          */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSCHKN),
                       (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSCHKN)) |
                        (SCSI_UEXACT8)SCSI_SHVALIDSTDIS));
      }
#endif /* !SCSI_DCH_U320_MODE */

#if (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)
      /* When running on Harpoon 2 Rev B, sequencer generates data overrun */
      /* at the end of the command if there is a Wide Residue byte left in */
      /* the data FIFO and the target does not issue ignore wide residue   */
      /* message.  The behavior is due to the H2B change where ASIC does   */
      /* not report the emptied fifo in this condition.                    */
      /* To make the logic work, we need to put the Harpoon2 Rev B ASIC in */
      /* the backward compatible mode. */
      if ((hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320) ||
          (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320))
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSCHKN),
            (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSCHKN)) |
             (SCSI_UEXACT8)SCSI_WIDERESEN));
      }
#endif /* SCSI_STANDARD_U320_MODE */
   }
#endif /* !SCSI_ASPI_REVA_SUPPORT */
   /* Restore the incoming mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
}

/*********************************************************************
*
*  SCSIhStandardAbortChannel
*
*     Abort active hiobs associated with channel for standard mode
*
*  Return Value:  none
*
*  Parameters:    hhcb
*
*  Remarks:
*
*********************************************************************/
#if SCSI_STANDARD_MODE
void SCSIhStandardAbortChannel (SCSI_HHCB SCSI_HPTR hhcb,
                                SCSI_UEXACT8 haStatus)
{
   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_UEXACT16 i;
#if SCSI_RAID1 
   SCSI_BOOLEAN raid1Scb;
#endif /* SCSI_RAID1 */  


   for (i = 0; i < hhcb->numberScbs; i++)
   {
      if ((hiob = SCSI_ACTPTR[i]) != SCSI_NULL_HIOB)
      {
#if SCSI_RAID1
         raid1Scb = SCSI_FALSE;
         if (hiob->SCSI_IF_raid1)
         {
            /* Is the SCB for the primary */		
            /* Or the mirror target */
            if (i == hiob->scbNumber)
            {
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
               /* PCI error handling may have already set haStat */
               if (hiob->mirrorHiob->haStat != SCSI_HOST_PCI_OR_PCIX_ERROR)
               {
                  hiob->mirrorHiob->haStat = haStatus;
               }
#else
               hiob->mirrorHiob->haStat = haStatus;
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */
               hiob->mirrorHiob->stat = SCSI_SCB_ABORTED;
               SCSI_ACTPTR[hiob->mirrorHiob->scbNumber] = SCSI_NULL_HIOB;
            }
            else
            {
               if (i == hiob->mirrorHiob->scbNumber)
               {
                  raid1Scb = SCSI_TRUE;
                  /* Update the relevant fields in the mirror hiob */
                  hiob->mirrorHiob->cmd = hiob->cmd; 
                  hiob->mirrorHiob->flags = hiob->flags; 
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
         if (hiob->SCSI_IP_targetMode ||
             hiob->cmd == SCSI_CMD_ESTABLISH_CONNECTION)
         {
            if (SCSI_NEXUS_UNIT(hiob)->hhcb != hhcb)
            {
               continue; 
            }
         }    
         else
#endif /* SCSI_TARGET_OPERATION */
         if (SCSI_TARGET_UNIT(hiob)->hhcb != hhcb)
         {
            continue;
         }   

         hiob->stat = SCSI_SCB_ABORTED;
         
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
         /* PCI error handling may have already set haStat */
         if (hiob->haStat != SCSI_HOST_PCI_OR_PCIX_ERROR)
         {
            hiob->haStat = haStatus;
         }
#else
         hiob->haStat = haStatus;
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */
         
         SCSI_ACTPTR[i] = SCSI_NULL_HIOB;

#if SCSI_RAID1
         if (!raid1Scb)
         {
            SCSI_hFREESCBDESCRIPTOR(hhcb,hiob);
         }
#else
         SCSI_hFREESCBDESCRIPTOR(hhcb,hiob);
#endif /* SCSI_RAID1 */
         if (hiob->cmd == SCSI_CMD_INITIATE_TASK)
         {
#if SCSI_RAID1
            if (!raid1Scb)
#endif /* SCSI_RAID1 */
            {
               SCSI_COMPLETE_HIOB(hiob);
            }
         }
         else
         {
            SCSI_COMPLETE_SPECIAL_HIOB(hiob);
         }
      }
   }
}
#endif /* SCSI_STANDARD_MODE */

/*********************************************************************
*
*  SCSIhDownshiftAbortChannel
*
*     Abort active hiobs associated with channel for downshift mode
*
*  Return Value:  none
*
*  Parameters:    hhcb
*
*  Remarks:
*
*********************************************************************/
#if SCSI_DOWNSHIFT_MODE
void SCSIhDownshiftAbortChannel (SCSI_HHCB SCSI_HPTR hhcb,
                                 SCSI_UEXACT8 haStatus)
{
   SCSI_HIOB SCSI_IPTR hiob;

#if SCSI_BIOS_SUPPORT
   SCSI_UEXACT8 databuffer[4];
   SCSI_UEXACT8 j;
   SCSIHReadScratchRam(hhcb,
                       (SCSI_UEXACT8 SCSI_SPTR) &hiob,
                       (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ACTIVE_HIOB),
                       (SCSI_UEXACT16) 4);
#else
   hiob = SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET];
#endif /* SCSI_BIOS_SUPPORT */

   /* valid hiob? */
   if (hiob != SCSI_NULL_HIOB)
   {
      hiob->stat = SCSI_SCB_ABORTED;
      hiob->haStat = haStatus;
   
#if SCSI_BIOS_SUPPORT
      for (j = 0; j < 4; j++)
      {
         databuffer[j] = SCSI_NULL_ENTRY;      
      }

      SCSIHWriteScratchRam(hhcb,
                           (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ACTIVE_HIOB),
                           (SCSI_UEXACT16) 4,
                           (SCSI_UEXACT8 SCSI_IPTR) &databuffer);
#else
      SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET] = SCSI_NULL_HIOB;
#endif /* SCSI_BIOS_SUPPORT */

      /* Free the SCB descriptor - if required. */
      SCSI_hFREESCBDESCRIPTOR(hhcb,hiob);

      if (hiob->cmd == SCSI_CMD_INITIATE_TASK)
      {
         SCSI_COMPLETE_HIOB(hiob);
      }
   
/* Remove if Downshift doesn't require SCSI_COMPLETE_SPECIAL_HIOB cmds */
   
      else
      {
         SCSI_COMPLETE_SPECIAL_HIOB(hiob);
      }
   }      
}
#endif /* SCSI_DOWNSHIFT_MODE */

/*********************************************************************
*
*  SCSIhStandardU320ResetSoftware
*
*     Reset channel software for standard U320 mode
*
*  Return Value:  none
*
*  Parameters:    hhcb
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*
*********************************************************************/
#if (SCSI_STANDARD_U320_MODE || SCSI_STANDARD_ENHANCED_U320_MODE)
void SCSIhStandardU320ResetSoftware (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_SCB_DESCRIPTOR SCSI_HPTR scbDescriptor;
   SCSI_BUS_ADDRESS busAddress;
   SCSI_UEXACT16 tnScbs;
   SCSI_UEXACT16 i;
   SCSI_UEXACT8 tvalue;
   SCSI_UEXACT8 mode;
   SCSI_UEXACT8 modeSave;

   /* Set Mode to 3 for now. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* setup q_new_pointer */
#if SCSI_SCBBFR_BUILTIN
   /* Obtain first scb descriptor from free scb descriptor queue. */
   scbDescriptor = hhcb->SCSI_FREE_HEAD;
#else
   /* Request first scb descriptor from OSM. */
   scbDescriptor = SCSI_GET_FREE_HEAD(hhcb);
#endif /* SCSI_SCBBFR_BUILTIN */
   SCSI_hSETADDRESSSCRATCH(hhcb,SCSI_SU320_Q_NEW_POINTER,
                           scbDescriptor->scbBuffer.virtualAddress);

   /* setup host new SCB queue offset */
   OSD_OUTEXACT16(SCSI_AICREG(SCSI_HNSCB_QOFF),(SCSI_UEXACT16)0);
   hhcb->SCSI_HP_hnscbQoff = 0;

   /* setup q_done_base */
   SCSI_hSETADDRESSSCRATCH(hhcb,SCSI_SU320_Q_DONE_BASE,SCSI_QOUT_PTR_ARRAY_NEW(hhcb));

   /* Initialize Q_DONE_POINTER with lo 4 bytes of done queue address. The hi */
   /* 4 bytes will come from Q_DONE_BASE when the sequencer uses              */
   /* Q_DONE_POINTER so we only need to init. the lo 4 bytes. */
   /* Get bus address and load it to scratch ram */
#if SCSI_ASPI_SUPPORT
   OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,SCSI_QOUT_PTR_ARRAY_NEW(hhcb));
   busAddress = hhcb->busAddress;
#else
   busAddress = OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,SCSI_QOUT_PTR_ARRAY_NEW(hhcb));
#endif /* SCSI_ASPI_SUPPORT */
#if (OSD_BUS_ADDRESS_SIZE == 32)
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_DONE_POINTER+0),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_DONE_POINTER+1),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order1);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_DONE_POINTER+2),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order2);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_DONE_POINTER+3),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order3);

   /* Harpoon2 Rev A has a problem wherein it requires the S/G List Address */
   /* to be on a 8-byte boundary.  Workaround has been done in the          */
   /* sequencer to adjust the address by moving to the nearest 8-byte       */
   /* boundary and do the proper math to get to the elements.  To do this   */
   /* the sequencer requires that the automatic address increment to the    */
   /* next S/G element be turned off.  Even though this affects only the    */
   /* address size of 8-bytes, to keep it simple this automatic increment   */
   /* feature has been turned off for both the 4-byte and 8-byte addresses. */ 
   /* The details of the hardware problem can be found in the Razor database*/
   /* entry #516. */
#if SCSI_STANDARD_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320)
   {
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST),
         (OSD_INEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST)) & ~SCSI_SG_ELEMENT_SIZE));

      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   }
#endif /* SCSI_STANDARD_U320_MODE */
#endif /* (OSD_BUS_ADDRESS_SIZE == 32) */

#if (OSD_BUS_ADDRESS_SIZE == 64)
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_DONE_POINTER+0),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_DONE_POINTER+1),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order1);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_DONE_POINTER+2),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order2);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_DONE_POINTER+3),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order3);

   /* set up a flag indicates 32/64 bit scatter gather list format */

   /* Update sgsegment_32 flag */
   if (hhcb->SCSI_HF_SgBusAddress32)
   {
      /* 32-bit s/g list format */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS)) | SCSI_SU320_SGSEGMENT_32));

      /* Set Mode to 4 for now. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST),
         (OSD_INEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST)) & ~SCSI_SG_ELEMENT_SIZE));
   }
   else
   {
      /* 64-bit s/g list format */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS)) & ~SCSI_SU320_SGSEGMENT_32));
      
      /* Harpoon2 Rev A has a problem wherein it requires the S/G List       */
      /* Address to be on a 8-byte boundary.  Workaround has been done in    */
      /* the sequencer to adjust the address by moving to the nearest 8-     */
      /* byte boundary and do the proper math to get to the elements.  To do */
      /* this the sequencer requires that the automatic address increment to */
      /* the next S/G element be turned off.  Even though this affects only  */
      /* the address size of 8-bytes, to keep it simple this automatic       */
      /* increment feature has been turned off for both the 4-byte and 8-byte*/
      /* addresses.  The details of the hardware problem can be found in the */
      /* Razor database entry #516. */
#if SCSI_STANDARD_U320_MODE
      /* If using standard sequencer then turn off increment */
      if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320)
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST),
            (OSD_INEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST)) & ~SCSI_SG_ELEMENT_SIZE));
      }
      else
#endif /* SCSI_STANDARD_U320_MODE */
      {
         /* Set Mode to 4 for now. */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST),
            (OSD_INEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST)) | SCSI_SG_ELEMENT_SIZE));
      }
   }

   /* Set Mode to 3 for now. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Clear the ARPINTCODE valid bit as a part of the workaround for Harpoon2*/
   /* RevB hardware problem (razor issue# 904). */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_hARPINTVALID_REG(hhcb)),
      OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_hARPINTVALID_REG(hhcb))) &
      ~(SCSI_hARPINTVALID_BIT(hhcb)));

#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */

   /* Initialize the HARPOON2_REVB_ASIC flag to approriate value depending  */
   /* on whether the Harpoon2 ASIC is Rev A or Rev B. */
#if !SCSI_ASPI_REVA_SUPPORT
   if (SCSI_hHARPOON_REV_B(hhcb))
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS)) |
         SCSI_SU320_HARPOON2_REVB_ASIC));
   }
   else
#endif /* !SCSI_ASPI_REVA_SUPPORT */
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS)) &
         ~SCSI_SU320_HARPOON2_REVB_ASIC));
   }

#if SCSI_RAID1
    OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS),
                  (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS)) | 
                   SCSI_SU320_MIRROR_RAID1));
#else /* !SCSI_RAID1 */
    OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS),
                  (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SEQ_OPTIONS)) & 
                   ~SCSI_SU320_MIRROR_RAID1));
#endif /* SCSI_RAID1 */

   /* Set interrupt threshold timeout value.  */
   /* Note that both locations are set to the */
   /* same value.                             */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_INTR_THRSH_TIMEOUT1),
                 hhcb->cmdCompleteFactorThreshold);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_INTR_THRSH_TIMEOUT2),
                 hhcb->cmdCompleteFactorThreshold);

   /* Set interrupt threshold maxcount value.  */
   /* Note that both locations are set to the */
   /* same value.                             */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_INTR_THRSH_MAXCNT1),
                 hhcb->cmdCompleteThresholdCount);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_INTR_THRSH_MAXCNT2),
                 hhcb->cmdCompleteThresholdCount);

   /* setup q_done_head */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_DONE_HEAD),(SCSI_UEXACT8)0xFF);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_DONE_HEAD+1),(SCSI_UEXACT8)0xFF);

#if SCSI_STANDARD_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320)
   {
      /* setup 'scratch_flags' register */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_SCRATCH_FLAGS),
                      (SCSI_UEXACT8)SCSI_SU320_SG_CACHE_AVAIL);
   }
#endif /* SCSI_STANDARD_U320_MODE */

   /* setup q_done_element */
   hhcb->SCSI_HP_qDoneElement = 0;

   /* setup q_done_pass (both scratch and shadow for HIM */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_DONE_PASS),(SCSI_UEXACT8) 0x00);
   hhcb->SCSI_HP_qDonePass = 0;
   
   SCSI_hTARGETRESETSOFTWARE(hhcb);

   /* setup q_exe_head */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD),(SCSI_UEXACT8)0xFF);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1),(SCSI_UEXACT8)0xFF);

   for (i = 0; i < 32; i++)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+i),(SCSI_UEXACT8)0xFF);
   }

   /* setup done SCB queue in host */
   for (i = 0; i < (hhcb->totalDoneQElements * sizeof(SCSI_QOUTFIFO_NEW)); i++)
   {
      *(((SCSI_UEXACT8 SCSI_HPTR) SCSI_QOUT_PTR_ARRAY_NEW(hhcb)) + i) = 
                              (SCSI_UEXACT8) 0xFF;
   }

   /* Invalidate waiting scb and active scb entry */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Initialize all the mode-dependent scratch registers */
   for (mode = 0; mode <= 4; mode++)
   {
      SCSI_hSETMODEPTR(hhcb,scsiRegister,(mode | (mode<<4)));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SU320_ACTIVE_SCB+1),(SCSI_UEXACT8)SCSI_NULL_SCB);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SU320_RET_ADDR),(SCSI_UEXACT8)0xFF);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SU320_RET_ADDR+1),(SCSI_UEXACT8)0xFF);
   }
   for (mode = 0; mode <= 1; mode++)
   {
      SCSI_hSETMODEPTR(hhcb,scsiRegister,(mode | (mode<<4)));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SU320_SG_STATUS),(SCSI_UEXACT8)0);
   }

   /* setup Arp new SCB queue offset & done SCB queue offset, */
   /* which are in mode 2 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF),(SCSI_UEXACT8)0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF+1),(SCSI_UEXACT8)0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SDSCB_QOFF),(SCSI_UEXACT8)0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SDSCB_QOFF+1),(SCSI_UEXACT8)0);

   /* Program the appropriate encoded value for the SCSI_SCB_QSIZE bits in */
   /* mode 2. */
   tnScbs = SCSI_MINDONEQELEMENTS;
   tvalue = 0;
   while (tnScbs < hhcb->totalDoneQElements)
   {
      tnScbs *= 2;
      tvalue++;
   }
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_QOFF_CTLSTA),
      ((OSD_INEXACT8(SCSI_AICREG(SCSI_QOFF_CTLSTA)) &
      ~SCSI_SCB_QSIZE) | tvalue));

   /* Load all the necessary offsets into the SCB into their respective */
   /* registers. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
#if SCSI_PACKETIZED_IO_SUPPORT
   /* These registers are only required for packetized operation. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LUNPTR), SCSI_SU320_SLUN_OFFSET);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_QNEXTPTR), SCSI_SU320_Q_EXETARG_NEXT_OFFSET);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LUNLEN), SCSI_hSLUN_LENGTH(hhcb)); 
#if SCSI_INITIATOR_OPERATION
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CDBLENPTR), SCSI_SU320_SCDB_LENGTH_OFFSET);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_ATTRPTR), SCSI_SU320_TASK_ATTRIBUTE_OFFSET);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_FLAGPTR), SCSI_SU320_TASK_MANAGEMENT_OFFSET);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CDBPTR), SCSI_SU320_SCDB_OFFSET);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_ABRTBYTEPTR), SCSI_SU320_SCONTROL_OFFSET);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_ABRTBITPTR), SCSI_SU320_SCONTROL_ABORT);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_MAXCMD), (SCSI_UEXACT8)0xFF);

   /* null out SCB reserve identifier for IU status buffer */
   SCSI_SET_NULL_SCB(hhcb->SCSI_SPI_STATUS_IU_SCB[0]);
   SCSI_SET_NULL_SCB(hhcb->SCSI_SPI_STATUS_IU_SCB[1]);
#endif /* SCSI_INITIATOR_OPERATION */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

#if SCSI_STANDARD_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBAUTOPTR),
          (SCSI_AUSCBPTR_EN | OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_array_site)));
   }
#endif /* SCSI_STANDARD_U320_MODE */

#if SCSI_STANDARD_ENHANCED_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBAUTOPTR),
          (SCSI_AUSCBPTR_EN | OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_array_site)));
   }
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */
   
   /* Set ARP Interrupt #1 Vector */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_INTVECT1_ADDR0),
                 0xFF & (SCSI_hISR(hhcb) >> 2));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_INTVECT1_ADDR1),
                 (0xFF00 & (SCSI_hISR(hhcb) >> 2)) >> 8);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   /* set active pointer array to null */
   for (i = 0; i < hhcb->numberScbs; i++)
   {
      SCSI_ACTPTR[i] = SCSI_NULL_HIOB;
   }

   /* reset software common to all modes of operations */
   SCSIhCommonResetSoftware(hhcb);
}
#endif /* SCSI_STANDARD_MODE */

/*********************************************************************
*
*  SCSIhDownshiftU320ResetSoftware
*
*     Reset channel software for downshift U320 mode
*
*  Return Value:  none
*
*  Parameters:    hhcb
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*
*********************************************************************/
#if SCSI_DOWNSHIFT_MODE
void SCSIhDownshiftU320ResetSoftware (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 numberScbs = hhcb->numberScbs;
   SCSI_UEXACT16 tnScbs;
   SCSI_UEXACT16 i;
   SCSI_UEXACT8 tvalue;
   SCSI_UEXACT8 mode;
   SCSI_UEXACT8 modeSave;

   SCSI_UEXACT16 downshift_hnscbQoff = 0;

#if SCSI_BIOS_SUPPORT
   SCSI_UEXACT8 databuffer[4];
#else
   SCSI_SCB_DESCRIPTOR SCSI_HPTR scbDescriptor;
#endif /* !SCSI_BIOS_SUPPORT */

   /* Set Mode to 3 for now. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

#if !SCSI_BIOS_SUPPORT

   /* setup q_new_pointer */
#if SCSI_SCBBFR_BUILTIN
   /* Obtain first scb descriptor from free scb descriptor queue. */
   scbDescriptor = hhcb->SCSI_FREE_HEAD;
#else
   /* Request first scb descriptor from OSM. */
   scbDescriptor = SCSI_GET_FREE_HEAD(hhcb);
#endif /* SCSI_SCBBFR_BUILTIN */
#if SCSI_EFI_BIOS_SUPPORT
   SCSI_hSETADDRESSSCRATCH(hhcb,SCSI_DU320_Q_NEW_POINTER,
                           scbDescriptor->scbBuffer.busAddress);
#else
   SCSI_hSETADDRESSSCRATCH(hhcb,SCSI_DU320_Q_NEW_POINTER,
                           scbDescriptor->scbBuffer.virtualAddress);
#endif /* SCSI_EFI_BIOS_SUPPORT */
#endif /* !SCSI_BIOS_SUPPORT */

   /* Initialize the HARPOON2_REVB_ASIC flag to approriate value depending  */
   /* on whether the Harpoon2 ASIC is Rev A or Rev B. */
   if (SCSI_hHARPOON_REV_B(hhcb))
   {
   
      /* Initialize the ASPI_BIOS_COMMAND scratch location */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_ASPI_BIOS_COMMAND),SCSI_DU320_HARPOON2_REVB_ASIC);

   }
   else
      /* Clear the Command requestor type field */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_ASPI_BIOS_COMMAND),0);

   /* Clear the ARPINTCODE valid bit as a part of the workaround for Harpoon2*/
   /* RevB hardware problem (razor issue# 904). */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_hARPINTVALID_REG(hhcb)),
      OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_hARPINTVALID_REG(hhcb))) &
      ~(SCSI_hARPINTVALID_BIT(hhcb)));

   /* clear the nego on mode switch */
   
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_NEGO_ON_MODE_SWITCH),0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_NEGO_ON_MODE_SWITCH+1),0);
                                 
   /* setup host new SCB queue offset */
   OSD_OUTEXACT16(SCSI_AICREG(SCSI_HNSCB_QOFF),(SCSI_UEXACT16)0);


   SCSIHWriteScratchRam(hhcb,
                        (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_DOWNSHIFT_HNSCBQOFF),
                        (SCSI_UEXACT16) 2,
                        (SCSI_UEXACT8 SCSI_IPTR) &downshift_hnscbQoff);


   /* setup q_done_head */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_Q_DONE_HEAD),(SCSI_UEXACT8)0xFF);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_Q_DONE_HEAD+1),(SCSI_UEXACT8)0xFF);

   /* setup 'scratch_flags' register */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_SCRATCH_FLAGS),
         (SCSI_UEXACT8)SCSI_DU320_SG_CACHE_AVAIL);

   /* setup q_exe_head */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_Q_EXE_HEAD),(SCSI_UEXACT8)0xFF);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_Q_EXE_HEAD+1),(SCSI_UEXACT8)0xFF);

   for (i = 0; i < 32; i++)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_Q_EXETARG_TAIL+i),(SCSI_UEXACT8)0xFF);
   }

   /* Invalidate waiting scb and active scb entry */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Initialize all the mode-dependent scratch registers */
   for (mode = 0; mode <= 4; mode++)
   {
      SCSI_hSETMODEPTR(hhcb,scsiRegister,(mode | (mode<<4)));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DU320_ACTIVE_SCB+1),(SCSI_UEXACT8)SCSI_NULL_SCB);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DU320_RET_ADDR),(SCSI_UEXACT8)0xFF);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DU320_RET_ADDR+1),(SCSI_UEXACT8)0xFF);
   }
   for (mode = 0; mode <= 1; mode++)
   {
      SCSI_hSETMODEPTR(hhcb,scsiRegister,(mode | (mode<<4)));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DU320_SG_STATUS),(SCSI_UEXACT8)0);
   }

   /* setup arp new SCB queue offset & done SCB queue offset, */
   /* which are in mode 2 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF),(SCSI_UEXACT8)0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF+1),(SCSI_UEXACT8)0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SDSCB_QOFF),(SCSI_UEXACT8)0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SDSCB_QOFF+1),(SCSI_UEXACT8)0);

   /* Program the appropriate encoded value for the SCSI_SCB_QSIZE bits in */
   /* mode 2. */
   tnScbs = SCSI_MINDONEQELEMENTS;
   tvalue = 0;
   while (tnScbs < hhcb->totalDoneQElements)
   {
      tnScbs *= 2;
      tvalue++;
   }
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_QOFF_CTLSTA),
      ((OSD_INEXACT8(SCSI_AICREG(SCSI_QOFF_CTLSTA)) &
      ~SCSI_SCB_QSIZE) | tvalue));

   /* Load all the necessary offsets into the SCB into their respective */
   /* registers. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

#if SCSI_DOWNSHIFT_U320_MODE
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBAUTOPTR),
          (SCSI_AUSCBPTR_EN | OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_array_site)));
   }
#endif /* SCSI_DOWNSHIFT_U320_MODE */

#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE      
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBAUTOPTR),
          (SCSI_AUSCBPTR_EN | OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_array_site)));

#if (OSD_BUS_ADDRESS_SIZE == 64)
      /* Harpoon2 Rev A has a problem wherein it requires the S/G List       */
      /* Address to be on a 8-byte boundary.  Workaround has been done in    */
      /* the sequencer to adjust the address by moving to the nearest 8-     */
      /* byte boundary and do the proper math to get to the elements.  To do */
      /* this the sequencer requires that the automatic address increment to */
      /* the next S/G element be turned off.  Even though this affects only  */
      /* the address size of 8-bytes, to keep it simple this automatic       */
      /* increment feature has been turned off for both the 4-byte and 8-byte*/
      /* addresses.  The details of the hardware problem can be found in the */
      /* Razor database entry #516. */

      /* The workaround is required for Downshift U320 mode.  The automatic  */
      /* increment feature is disabled by default.  Therefore, CHIM does not */
      /* have to disable it under Downshift U320 mode.  But, CHIM must enable*/
      /* the automatic increment feature for Downshift Enhanced U32 in order */
      /* to work properly with 8-byte S/G address. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST),
         (OSD_INEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST)) | SCSI_SG_ELEMENT_SIZE));
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */
   }
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

#if SCSI_BIOS_SUPPORT

   for (i = 0; i < 4; i++)
   {
      databuffer[i] = SCSI_NULL_ENTRY;
   }

   SCSIHWriteScratchRam(hhcb,
                        (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ACTIVE_HIOB),
                        (SCSI_UEXACT16) 4,
                        (SCSI_UEXACT8 SCSI_IPTR) &databuffer);

#else /* !SCSI_BIOS_SUPPORT */

   SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET] = SCSI_NULL_HIOB;

#endif /* SCSI_BIOS_SUPPORT */

   /* reset software common to all modes of operations */
   SCSIhCommonResetSoftware(hhcb);
}
#endif /* SCSI_DOWNSHIFT_MODE */

/*********************************************************************
*
*  SCSIhDchU320ResetSoftware
*
*     Reset channel software for standard U320 mode
*
*  Return Value:  none
*
*  Parameters:    hhcb
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320ResetSoftware (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_SCB_DESCRIPTOR SCSI_HPTR scbDescriptor;
   SCSI_BUS_ADDRESS busAddress;
   SCSI_UEXACT16 tnScbs;
   SCSI_UEXACT16 i;
   SCSI_UEXACT8 tvalue;
   SCSI_UEXACT8 mode;
   SCSI_UEXACT8 modeSave;

   /* Set Mode to 3 for now. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* setup q_new_pointer */
#if SCSI_SCBBFR_BUILTIN
   /* Obtain first scb descriptor from free scb descriptor queue. */
   scbDescriptor = hhcb->SCSI_FREE_HEAD;
#else
   /* Request first scb descriptor from OSM. */
   scbDescriptor = SCSI_GET_FREE_HEAD(hhcb);
#endif /* SCSI_SCBBFR_BUILTIN */
   SCSI_hSETADDRESSSCRATCH(hhcb,SCSI_DCHU320_Q_NEW_POINTER,
                           scbDescriptor->scbBuffer.virtualAddress);

   /* We must also clear the upper 4 bytes even though we are not a 64 bit address bus */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_NEW_POINTER+4),0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_NEW_POINTER+5),0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_NEW_POINTER+6),0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_NEW_POINTER+7),0);

   /* setup host new SCB queue offset */
   OSD_OUTEXACT16(SCSI_AICREG(SCSI_HNSCB_QOFF),(SCSI_UEXACT16)0);
   hhcb->SCSI_HP_hnscbQoff = 0;

   /* setup q_done_base */
   SCSI_hSETADDRESSSCRATCH(hhcb,SCSI_DCHU320_Q_DONE_BASE,SCSI_QOUT_PTR_ARRAY_NEW(hhcb));

   /* We must also clear the upper 4 bytes even though we are not a 64 bit address bus */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_BASE+4),0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_BASE+5),0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_BASE+6),0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_BASE+7),0);

   /* Initialize Q_DONE_POINTER with lo 4 bytes of done queue address. The hi */
   /* 4 bytes will come from Q_DONE_BASE when the sequencer uses              */
   /* Q_DONE_POINTER so we only need to init. the lo 4 bytes. */
   /* Get bus address and load it to scratch ram */
   busAddress = OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,SCSI_QOUT_PTR_ARRAY_NEW(hhcb));

#if (OSD_BUS_ADDRESS_SIZE == 32)
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_POINTER+0),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_POINTER+1),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order1);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_POINTER+2),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order2);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_POINTER+3),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order3);

#endif /* (OSD_BUS_ADDRESS_SIZE == 32) */

/******  Most likely will be removed $$ DCH $$ ******/ 

#if (OSD_BUS_ADDRESS_SIZE == 64)
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_POINTER+0),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_POINTER+1),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order1);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_POINTER+2),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order2);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_POINTER+3),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order3);

   /* set up a flag indicates 32/64 bit scatter gather list format */

   /* Update sgsegment_32 flag */
   if (hhcb->SCSI_HF_SgBusAddress32)
   {
      /* 32-bit s/g list format */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_SEQ_OPTIONS),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_SEQ_OPTIONS)) | SCSI_DCHU320_SGSEGMENT_32));

      /* Set Mode to 4 for now. */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST),
         (OSD_INEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST)) & ~SCSI_SG_ELEMENT_SIZE));
   }
   else
   {
      /* 64-bit s/g list format */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_SEQ_OPTIONS),
         (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_SEQ_OPTIONS)) & ~SCSI_DCHU320_SGSEGMENT_32));
      
      /* Harpoon2 Rev A has a problem wherein it requires the S/G List       */
      /* Address to be on a 8-byte boundary.  Workaround has been done in    */
      /* the sequencer to adjust the address by moving to the nearest 8-     */
      /* byte boundary and do the proper math to get to the elements.  To do */
      /* this the sequencer requires that the automatic address increment to */
      /* the next S/G element be turned off.  Even though this affects only  */
      /* the address size of 8-bytes, to keep it simple this automatic       */
      /* increment feature has been turned off for both the 4-byte and 8-byte*/
      /* addresses.  The details of the hardware problem can be found in the */
      /* Razor database entry #516. */
      if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
      {
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST),
            (OSD_INEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST)) & ~SCSI_SG_ELEMENT_SIZE));
      }
      else
      {
         /* Set Mode to 4 for now. */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST),
            (OSD_INEXACT8(SCSI_AICREG(SCSI_CMC_RAMBIST)) | SCSI_SG_ELEMENT_SIZE));
      }
   }

   /* Set Mode to 3 for now. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */

   /* Initialize DCH CORE to a HARPOON2_REVB_ASIC flag. */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_SEQ_OPTIONS),
      (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_SEQ_OPTIONS)) |
      SCSI_DCHU320_HARPOON2_REVB_ASIC));


#if SCSI_RAID1
    OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_SEQ_OPTIONS),
                  (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_SEQ_OPTIONS)) | 
                   SCSI_DCHU320_MIRROR_RAID1));
#else /* !SCSI_RAID1 */
    OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_SEQ_OPTIONS),
                  (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_SEQ_OPTIONS)) & 
                   ~SCSI_DCHU320_MIRROR_RAID1));
#endif /* SCSI_RAID1 */

   /* Set interrupt threshold timeout value.  */
   /* Note that both locations are set to the */
   /* same value.                             */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_INTR_THRSH_TIMEOUT1),
                 hhcb->cmdCompleteFactorThreshold);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_INTR_THRSH_TIMEOUT2),
                 hhcb->cmdCompleteFactorThreshold);

   /* Set interrupt threshold maxcount value.  */
   /* Note that both locations are set to the */
   /* same value.                             */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_INTR_THRSH_MAXCNT1),
                 hhcb->cmdCompleteThresholdCount);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_INTR_THRSH_MAXCNT2),
                 hhcb->cmdCompleteThresholdCount);

   /* setup q_done_head */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_HEAD),(SCSI_UEXACT8)0xFF);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_HEAD+1),(SCSI_UEXACT8)0xFF);

   /* setup q_done_element */
   hhcb->SCSI_HP_qDoneElement = 0;

   /* setup q_done_pass (both scratch and shadow for HIM */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_PASS),(SCSI_UEXACT8) 0x00);
   hhcb->SCSI_HP_qDonePass = 0;
   
   SCSI_hTARGETRESETSOFTWARE(hhcb);

   /* setup q_exe_head */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD),(SCSI_UEXACT8)0xFF);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1),(SCSI_UEXACT8)0xFF);

   for (i = 0; i < 32; i++)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+i),(SCSI_UEXACT8)0xFF);
   }

   /* setup done SCB queue in host */
   for (i = 0; i < (hhcb->totalDoneQElements * sizeof(SCSI_QOUTFIFO_NEW)); i++)
   {
      *(((SCSI_UEXACT8 SCSI_HPTR) SCSI_QOUT_PTR_ARRAY_NEW(hhcb)) + i) = 
                              (SCSI_UEXACT8) 0xFF;
   }

   /* Invalidate waiting scb and active scb entry */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Initialize all the mode-dependent scratch registers */
   for (mode = 0; mode <= 4; mode++)
   {
      SCSI_hSETMODEPTR(hhcb,scsiRegister,(mode | (mode<<4)));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHU320_ACTIVE_SCB+1),(SCSI_UEXACT8)SCSI_NULL_SCB);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHU320_RET_ADDR),(SCSI_UEXACT8)0xFF);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHU320_RET_ADDR+1),(SCSI_UEXACT8)0xFF);
   }
   for (mode = 0; mode <= 1; mode++)
   {
      SCSI_hSETMODEPTR(hhcb,scsiRegister,(mode | (mode<<4)));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DCHU320_SG_STATUS),(SCSI_UEXACT8)0);
   }

   /* setup Arp new SCB queue offset & done SCB queue offset, */
   /* which are in mode 2 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF),(SCSI_UEXACT8)0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF+1),(SCSI_UEXACT8)0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SDSCB_QOFF),(SCSI_UEXACT8)0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SDSCB_QOFF+1),(SCSI_UEXACT8)0);

   /* Program the appropriate encoded value for the SCSI_SCB_QSIZE bits in */
   /* mode 2. */
   tnScbs = SCSI_MINDONEQELEMENTS;
   tvalue = 0;
   while (tnScbs < hhcb->totalDoneQElements)
   {
      tnScbs *= 2;
      tvalue++;
   }
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_QOFF_CTLSTA),
      ((OSD_INEXACT8(SCSI_AICREG(SCSI_QOFF_CTLSTA)) &
      ~SCSI_SCB_QSIZE) | tvalue));

   /* Load all the necessary offsets into the SCB into their respective */
   /* registers. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
#if SCSI_PACKETIZED_IO_SUPPORT
   /* These registers are only required for packetized operation. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LUNPTR), SCSI_DCHU320_SLUN_OFFSET);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_QNEXTPTR), SCSI_DCHU320_Q_EXETARG_NEXT_OFFSET);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_LUNLEN), SCSI_hSLUN_LENGTH(hhcb)); 
#if SCSI_INITIATOR_OPERATION
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CDBLENPTR), SCSI_DCHU320_SCDB_LENGTH_OFFSET);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_ATTRPTR), SCSI_DCHU320_TASK_ATTRIBUTE_OFFSET);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_FLAGPTR), SCSI_DCHU320_TASK_MANAGEMENT_OFFSET);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CDBPTR), SCSI_DCHU320_SCDB_OFFSET);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_ABRTBYTEPTR), SCSI_DCHU320_SCONTROL_OFFSET);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_ABRTBITPTR), SCSI_DCHU320_SCONTROL_ABORT);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_MAXCMD), (SCSI_UEXACT8)0xFF); /* to say only one byte LUN */

   /* null out SCB reserve identifier for IU status buffer */
   SCSI_SET_NULL_SCB(hhcb->SCSI_SPI_STATUS_IU_SCB[0]);
   SCSI_SET_NULL_SCB(hhcb->SCSI_SPI_STATUS_IU_SCB[1]);
#endif /* SCSI_INITIATOR_OPERATION */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBAUTOPTR),
       (SCSI_AUSCBPTR_EN | OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_array_site)));
   
   /* Set ARP Interrupt #1 Vector */
 
   /* DCH allows WORD rd/wr operations. */
   OSD_OUTEXACT16(SCSI_AICREG(SCSI_INTVECT1_ADDR0),
                             (SCSI_DCHU320_ISR >> 2));

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   /* set active pointer array to null */
   for (i = 0; i < hhcb->numberScbs; i++)
   {
      SCSI_ACTPTR[i] = SCSI_NULL_HIOB;
   }

   /* reset software common to all modes of operations */
   SCSIhCommonResetSoftware(hhcb);
}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhCommonResetSoftware
*
*     Reset software common to all modes of operations
*
*  Return Value:  none
*
*  Parameters:    hhcb
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*
*********************************************************************/
void SCSIhCommonResetSoftware (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_UEXACT16 i;
   SCSI_DEVICE SCSI_DPTR deviceTable;

#if SCSI_BIOS_SUPPORT
   if (!hhcb->SCSI_HF_biosRunTimeEnabled)
#endif /* SCSI_BIOS_SUPPORT */
   {
      for (i = 0; i < (SCSI_UEXACT16)hhcb->maxDevices; i++)
      {
         /* Apply the device table's current rate entry with the hardware rate */
         /* table.  The purpose is to make sure the rate value of the device   */
         /* table's current rate entry and the hardward rate table are in sync */
         /* in all cases: after any scsi events during run-time and after the  */
         /* detection of the Real Mode software (ie. BIOS, ASPI driver or BEF  */
         /* driver) exist during initialization.                               */
         deviceTable = &SCSI_DEVICE_TABLE(hhcb)[(SCSI_UEXACT8)i];
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
         if (SCSI_hHARPOON_REV_1B(hhcb) &&
             hhcb->SCSI_HF_targetMode &&
             (!hhcb->SCSI_HF_initiatorMode))
         {
            /* Eventually a for loop of SCSI enabled IDs would be required. */
            SCSI_hGETNEGODATA(hhcb,
                              (SCSI_UEXACT8)(i | (hhcb->hostScsiID << 4)),
                              SCSI_CURRENT_XFER(deviceTable));
         }
         else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
         {
            SCSI_hGETNEGODATA(hhcb,(SCSI_UEXACT8)i,SCSI_CURRENT_XFER(deviceTable));
         }

         /* Set the flag to initial value */
         deviceTable->SCSI_DF_switchPkt = 0;
      }
   }

   /* set disconnect and transfer option */
   SCSIhCheckSyncNego(hhcb);

   /* make it ready to assign scb buffer (from scb buffer array) */
   SCSI_hSETUPASSIGNSCBBUFFER(hhcb);
}

/*********************************************************************
*
*  SCSIhSetAddressScratchBA32
*
*     Translate virtual address to 32 bits bus address and
*     set the 32 bits bus address to scratch ram
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 scratch address
*                 virtual address
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 32) && !(SCSI_BIOS_MODE_SUPPORT)) 
void SCSIhSetAddressScratchBA32 (SCSI_HHCB SCSI_HPTR hhcb,
                                 SCSI_UEXACT16 scratchAddress,
                                 void SCSI_HPTR virtualAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;

#if SCSI_ASPI_SUPPORT
   /* get bus address */
   OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,virtualAddress);
   busAddress = hhcb->busAddress;
#else
   /* get bus address */
   busAddress = OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,virtualAddress);
#endif /* SCSI_ASPI_SUPPORT */
   /* load bus address to scratch ram */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+0),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+1),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order1);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+2),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order2);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+3),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order3);
}
#endif /* ((OSD_BUS_ADDRESS_SIZE == 32) && !(SCSI_BIOS_MODE_SUPPORT)) */

/*********************************************************************
*
*  SCSIhSetAddressScratchBA64
*
*     Translate virtual address to 64 bits bus address and
*     set the 64 bits bus address to scratch ram
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 scratch address
*                 virtual address
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 64) && !(SCSI_BIOS_MODE_SUPPORT))
void SCSIhSetAddressScratchBA64 (SCSI_HHCB SCSI_HPTR hhcb,
                                 SCSI_UEXACT16 scratchAddress,
                                 void SCSI_HPTR virtualAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;

#if SCSI_ASPI_SUPPORT
   /* get bus address */
   OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,virtualAddress);
   busAddress = hhcb->busAddress;
#else
   /* get bus address */
   busAddress = OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,virtualAddress);
#endif /* SCSI_ASPI_SUPPORT */   
   /* load bus address to scratch ram */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+0),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+1),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order1);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+2),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order2);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+3),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order3);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+4),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order4);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+5),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order5);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+6),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order6);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+7),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order7);
}
#endif /* ((OSD_BUS_ADDRESS_SIZE == 64) && !(SCSI_BIOS_MODE_SUPPORT)) */



/*********************************************************************
*
*  SCSIhSetAddressScratchBA32
*
*     Translate virtual address to 32 bits bus address and
*     set the 32 bits bus address to scratch ram
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 scratch address
*                 physical address
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 32) && (SCSI_EFI_BIOS_SUPPORT)) 
void SCSIhSetAddressScratchBA32 (SCSI_HHCB SCSI_HPTR hhcb,
                                 SCSI_UEXACT16 scratchAddress,
                                 SCSI_BUS_ADDRESS physicalAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;

   /* get bus address */
   busAddress = physicalAddress;

   /* load bus address to scratch ram */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+0),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+1),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order1);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+2),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order2);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+3),((SCSI_QUADLET SCSI_SPTR)&busAddress)->u8.order3);
}
#endif /* ((OSD_BUS_ADDRESS_SIZE == 32) && (SCSI_EFI_BIOS_SUPPORT)) */

/*********************************************************************
*
*  SCSIhSetAddressScratchBA64
*
*     Translate virtual address to 64 bits bus address and
*     set the 64 bits bus address to scratch ram
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 scratch address
*                 physical address
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 64) && (SCSI_EFI_BIOS_SUPPORT))
void SCSIhSetAddressScratchBA64 (SCSI_HHCB SCSI_HPTR hhcb,
                                 SCSI_UEXACT16 scratchAddress,
                                 SCSI_BUS_ADDRESS physicalAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;

   /* get bus address */
   busAddress = physicalAddress;

   /* load bus address to scratch ram */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+0),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+1),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order1);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+2),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order2);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+3),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order3);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+4),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order4);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+5),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order5);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+6),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order6);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+7),((SCSI_OCTLET SCSI_SPTR)&busAddress)->u8.order7);
}
#endif /* ((OSD_BUS_ADDRESS_SIZE == 64) && (SCSI_EFI_BIOS_SUPPORT)) */


/*********************************************************************
*
*  SCSIhGetAddressScratchBA32
*
*     Get 32 bits bus address from scratch ram
*
*  Return Value:  busAddress
*                  
*  Parameters:    hhcb
*                 scratch address
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 32) && !(SCSI_BIOS_SUPPORT) && !(SCSI_ASPI_SUPPORT))
SCSI_BUS_ADDRESS SCSIhGetAddressScratchBA32 (SCSI_HHCB SCSI_HPTR hhcb,
                                             SCSI_UEXACT16 scratchAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;

   /* load bus address from scratch ram */
   ((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order0 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+0));
   ((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order1 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+1));
   ((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order2 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+2));
   ((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order3 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+3));

   return(busAddress);
}
#endif /* OSD_BUS_ADDRESS_SIZE == 32 */



/*********************************************************************
*
*  SCSIhGetAddressScratchBA32
*
*     Get 32 bits bus address from scratch ram
*
*  Return Value:  busAddress
*                  
*  Parameters:    hhcb
*                 scratch address
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 32) && !(SCSI_BIOS_SUPPORT) && (SCSI_ASPI_SUPPORT))
void SCSIhGetAddressScratchBA32 (SCSI_HHCB SCSI_HPTR hhcb,
                                             SCSI_UEXACT16 scratchAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;

   /* load bus address from scratch ram */
   ((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order0 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+0));
   ((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order1 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+1));
   ((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order2 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+2));
   ((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order3 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+3));
   hhcb->busAddress = busAddress;
   
}
#endif /* OSD_BUS_ADDRESS_SIZE == 32 */


/*********************************************************************
*
*  SCSIhGetAddressScratchBA64
*
*     Get 64 bits bus address from scratch ram
*
*  Return Value:  busAddress
*                  
*  Parameters:    hhcb
*                 scratch address
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 64) && !(SCSI_BIOS_SUPPORT) && !(SCSI_ASPI_SUPPORT))
SCSI_BUS_ADDRESS SCSIhGetAddressScratchBA64 (SCSI_HHCB SCSI_HPTR hhcb,
                                             SCSI_UEXACT16 scratchAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;

   /* load bus address from scratch ram */
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order0 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+0));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order1 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+1));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order2 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+2));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order3 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+3));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order4 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+4));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order5 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+5));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order6 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+6));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order7 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+7));

   return(busAddress);
}
#endif /* OSD_BUS_ADDRESS_SIZE == 64 */



/*********************************************************************
*
*  SCSIhGetAddressScratchBA64
*
*     Get 64 bits bus address from scratch ram
*
*  Return Value:  busAddress
*                  
*  Parameters:    hhcb
*                 scratch address
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 64) && !(SCSI_BIOS_SUPPORT) && (SCSI_ASPI_SUPPORT))
void SCSIhGetAddressScratchBA64 (SCSI_HHCB SCSI_HPTR hhcb,
                                             SCSI_UEXACT16 scratchAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;

   /* load bus address from scratch ram */
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order0 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+0));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order1 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+1));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order2 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+2));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order3 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+3));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order4 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+4));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order5 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+5));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order6 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+6));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order7 = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+7));
   hhcb->busAddress = busAddress;
   
}
#endif /* OSD_BUS_ADDRESS_SIZE == 64 */


/*********************************************************************
*
*  SCSIhStandardU320ResetBTA
*
*     Reset busy target array for standard U320 mode
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*                  
*********************************************************************/
#if SCSI_STANDARD_MODE
void SCSIhStandardU320ResetBTA (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_UEXACT16 i,n;

   /* check how big is the busy target array */
   /* note that for now n is based on the physical size of the SCB RAM */
   /* in the future if command chaining for the RAID environment is */
   /* implemented, then we will need to base n on the combination of */
   /* hhcb->numberHWScbs and SCSI_hTARG_LUN_MASK0(hhcb) which defines */
   /* how a shared SCB RAM is partitioned among multiple host adapters. */
#if !SCSI_ASPI_SUPPORT
   n = hhcb->numberHWScbs;

   /* Make sure that the number of SCBs initialized is less than or equal to */
   /* 255, as Busy Targets Table uses only the first 256 SCBs */
   if (n > (SCSI_UEXACT16)SCSI_hBTASIZE(hhcb))
   {
   
      n = (SCSI_UEXACT16)SCSI_hBTASIZE(hhcb);
   
   }
#else

      n = (SCSI_UEXACT16)SCSI_hBTASIZE(hhcb);
   
#endif /* !SCSI_ASPI_SUPPORT */
   /* Switch to mode 3 as SCSIhStandardIndexClearBusy uses SCBPTR */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   for (i = 0; i < n; i++)
   {
      SCSIhStandardU320IndexClearBusy(hhcb,(SCSI_UEXACT8)i);
   }
}
#endif /* SCSI_STANDARD_MODE */

/*********************************************************************
*
*  SCSIhDownshiftU320ResetBTA
*
*     Reset busy target array for downshift U320 mode
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*                  
*********************************************************************/
#if SCSI_DOWNSHIFT_MODE
void SCSIhDownshiftU320ResetBTA (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_hBTATABLE(hhcb)), (SCSI_UEXACT8)SCSI_NULL_SCB);
}

#endif /* SCSI_DOWNSHIFT_MODE */

/*********************************************************************
*
*  SCSIhStandardU320TargetClearBusy
*
*     Clear target busy array for standard U320 mode
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Assumes sequencer is paused.              
*                  
*********************************************************************/
#if SCSI_STANDARD_MODE
void SCSIhStandardU320TargetClearBusy (SCSI_HHCB SCSI_HPTR hhcb,
                                       SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UNIT_CONTROL SCSI_IPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 index;
   SCSI_UEXACT8 savedMode;
   
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 - a known mode for SCBPTR access. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
            ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

   index = (SCSI_UEXACT8) targetUnit->scsiID;
   if (hhcb->SCSI_HF_multiTaskLun)
   {
      index += (SCSI_UEXACT8)(targetUnit->lunID * 16);
   }
   SCSIhStandardU320IndexClearBusy(hhcb,(SCSI_UEXACT8)index);

   /* Restore SCBPTR */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbptr>>8));

   /* Restore Mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
}
#endif /* SCSI_STANDARD_MODE */

/*********************************************************************
*
*  SCSIhDownshiftU320TargetClearBusy
*
*     Clear target busy array for downshift U320 mode
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Assumes sequencer is paused.              
*                  
*********************************************************************/
#if SCSI_DOWNSHIFT_MODE
void SCSIhDownshiftU320TargetClearBusy (SCSI_HHCB SCSI_HPTR hhcb,
                                        SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_hBTATABLE(hhcb)), (SCSI_UEXACT8)SCSI_NULL_SCB);
}
#endif /* SCSI_DOWNSHIFT_MODE */

/*********************************************************************
*
*  SCSIhStandardU320IndexClearBusy
*
*     Clear indexed target busy array for standard U320 mode
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Assumes SCSI_MODE_PTR already set prior to entry
*                 as caller may have saved SCSI_SCBPTR.
*                  
*********************************************************************/
#if SCSI_STANDARD_MODE
void SCSIhStandardU320IndexClearBusy (SCSI_HHCB SCSI_HPTR hhcb,
                                      SCSI_UEXACT8 index)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

   /* Busy targets table is still indexed by a byte; so we need to */
   /* only the low byte of the SCBPTR register. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), index);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), 0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_hBTATABLE(hhcb)), (SCSI_UEXACT8)SCSI_NULL_SCB);
}
#endif /* SCSI_STANDARD_MODE */

/*********************************************************************
*
*  SCSIhResetScsi
*
*     Reset SCSI bus
*
*  Return Value:  0 - if SCSI bus is good after the reset
*                 1 - otherwise
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine can be called either at initialization
*                 or exceptional condition
*                 Assume we are in Mode 3.
*                  
*********************************************************************/
SCSI_UEXACT8 SCSIhResetScsi (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 sstat1;
   SCSI_UEXACT8 simode0;
   SCSI_UEXACT8 simode2;
#if !SCSI_BIOS_SUPPORT
   SCSI_UEXACT8 i;
#endif /* !SCSI_BIOS_SUPPORT */
   SCSI_UEXACT8 returnValue = 0;
   SCSI_UEXACT8 modeSave;
   SCSI_UEXACT32 timerCount;   

   /* Clear the bus hung indicator */
   SCSI_hSETBUSHUNG(hhcb,0);

   /* Save incomming mode - for now. */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Disable all SCSI interrupts */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
   simode0 = OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE0));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE0), 0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE1), 0);
   simode2 = OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE2));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE2), 0);
   /* End of disable all SCSI interrupts */

   /* Clear interrupts */
#if !SCSI_DCH_U320_MODE
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), 
                 (SCSI_CLRBRKADRINT | SCSI_CLRSWTMINT |
                  SCSI_CLRSCSIINT | SCSI_CLRARPINT |
                  SCSI_CLRCMDINT | SCSI_CLRSPLTINT)); 
#else                  
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), 
                 (SCSI_CLRBRKADRINT | SCSI_CLRSWERRINT |
                  SCSI_CLRSCSIINT | SCSI_CLRARPINT)); 

#endif /* !SCSI_DCH_U320_MODE */

   /* Set mode 3. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   /* Disable selection in/out and reselection logic */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ0), 0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ1), 0);

   /* Disable any enabled data channels */
   /* Note in the future we may want to add a check on 
    * whether the channel was successfully disabled and
    * if not perform an ASIC reset. If this fails then
    * don't honor the SCSI bus reset - simply return.
    */
   SCSIhDisableDataChannel(hhcb,(SCSI_UEXACT8)SCSI_MODE0);
   SCSIhDisableDataChannel(hhcb,(SCSI_UEXACT8)SCSI_MODE1);

   /* Set mode 3. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   /* Assert RESET SCSI bus. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ0), SCSI_SCSIRSTO); 

   /* Delay by default 'reset hold time' */ 
   timerCount = SCSI_hGETTIMERCOUNT(hhcb,SCSI_RESET_HOLD_DEFAULT);
   SCSIhDelayCount25us(hhcb, timerCount);
  
#if SCSI_TRIDENT_WORKAROUND
   /* Do not remove this code until we verify that it's not
    * required for U320 ASIC.
    */
   /* Initialize SCSISIG to 0 in case the hardware has asserted
    * the SEL signal. This will occur on Trident if the selection
    * logic is enabled prior to the reset. Note that; setting
    * ENSELO to 0 does not terminate the selection logic. This
    * problem is due to errors in the hardware selection
    * logic when aborting a selection due to a bus reset.
    * Currenty the only solution is to disable the SEL assertion
    * by clearing the SCSISIG register.
    */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), 0x00);
#endif /* SCSI_TRIDENT_WORKAROUND */

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ0), 00);                 /* remove RESET SCSI bus */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRSCSIRSTI);   /* Patch for unexpected  */
                                                                  /* scsi interrupt        */
   
   /* Clear any possible pending scsi interrupt that may exists   */
   /* at this point.                                              */
   sstat1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT0), 0xff);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), (sstat1 & ~SCSI_PHASEMIS));

   /* clear interrupt */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),
                 SCSI_CLRARPINT | SCSI_CLRSCSIINT);

   /* delay 1 millisec */
   SCSIhDelayCount25us(hhcb,SCSI_hGETTIMERCOUNT(hhcb,1000));

   /* Restore SCSISEQ1 register. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ1), (SCSI_ENRSELI+SCSI_ENAUTOATNP));

   /* Restore simode(s). */
   /* Set mode 4. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE0), simode0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE1),
       (SCSI_ENSCSIPERR + SCSI_ENSELTIMO + SCSI_ENSCSIRST));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE2), simode2);
   SCSIhEnableIOErr(hhcb);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

#if !SCSI_BIOS_SUPPORT
   /* Set here for the PAC to invoke suppress negotiation. */
   /* This to make sure the internal Bus Scan always talk async. to devices */
   hhcb->SCSI_HF_resetSCSI = 1;

   for (i = 0; i < SCSI_MAXDEV; i++)
   {
      SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DF_resetSCSI = 1;
   }
#endif /* !SCSI_BIOS_SUPPORT */

#if SCSI_TARGET_OPERATION   
   /* If  target mode enabled need to reset the holding
    * SCSI bus indicator or SCSIhTargetClearNexus will fail
    */ 
   if (hhcb->SCSI_HF_targetMode && hhcb->SCSI_HF_targetScsiBusHeld)
   {
      hhcb->SCSI_HF_targetScsiBusHeld = 0;
      hhcb->SCSI_HP_nexusHoldingBus->SCSI_XF_busHeld = 0;
   }
#endif /* SCSI_TARGET_OPERATION */
   if (!returnValue)
   {
      if (!SCSIhBusWasDead(hhcb))
      {
         returnValue = 0;
      }
      else
      {
         returnValue = 1;
      }
   }
   return(returnValue);
}

/*********************************************************************
*
*  SCSIhScsiBusReset
*
*     Perform SCSI Bus Reset request
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
#if (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
void SCSIhScsiBusReset (SCSI_HHCB SCSI_HPTR hhcb,
                        SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 scbNumber;
   SCSI_HIOB SCSI_IPTR activeHiob;
   SCSI_UEXACT8 hcntrl;
#if (SCSI_TRIDENT_WORKAROUND && SCSI_TARGET_OPERATION)
   SCSI_UEXACT8 savedMode;
#endif /* (SCSI_TRIDENT_WORKAROUND && SCSI_TARGET_OPERATION) */

   /* Save the HCNTRL value. */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* pause the chip if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }
#if !SCSI_BIOS_MODE_SUPPORT
   /* It is a request from the OSM to return the active target at the */
   /* time of reset.  We also check to make sure it is a valid one. */
   /* Indicate no active HIOB to start with. */
   SCSI_ACTIVE_TARGET(hiob) = (void SCSI_UPTR) SCSI_NULL_UNIT_CONTROL;
#endif /* !SCSI_BIOS_MODE_SUPPORT */
   scbNumber = ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb)+1))) << 8) |
                (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb))));

   if (!SCSI_IS_NULL_SCB(scbNumber))
   {
#if SCSI_STANDARD_MODE  
      activeHiob = SCSI_ACTPTR[scbNumber];
#else 
      activeHiob = SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET];
#endif /* SCSI_STANDARD_U320_MODE */

#if !SCSI_BIOS_MODE_SUPPORT
      if (activeHiob != SCSI_NULL_HIOB)
      {
#if SCSI_TARGET_OPERATION
         if (!hiob->SCSI_IP_targetMode)
#endif /* SCSI_TARGET_OPERATION */
         {
            /* Get active targetUnit */
            SCSI_ACTIVE_TARGET(hiob) = (void SCSI_IPTR)SCSI_TARGET_UNIT(activeHiob);
         }
      }
#endif /* !SCSI_BIOS_MODE_SUPPORT */
   }

   /* Issue SCSI bus reset */
   if (!SCSIhResetScsi(hhcb))
   {
      hiob->stat = SCSI_SCB_COMP;
   }
   else
   {
      hiob->stat = SCSI_SCB_ERR;
          
      hiob->haStat = SCSI_HOST_ABT_CHANNEL_FAILED;
   
   }

#if 0
   /* Delay 2 seconds after scsi reset for slow devices to settle down. */

   SCSIhDelayCount25us(hhcb,
	SCSI_hGETTIMERCOUNT(hhcb,(hhcb->resetDelay*1000))); /* remove??? */
#endif

   /* Reset channel hardware's parameters */
   SCSIhResetChannelHardware(hhcb);

   /* Block upper layers from issuing Establish Connection HIOBs */
   SCSI_hTARGETHOLDESTHIOBS(hhcb,1);

   /* Abort any active HIOB(s) */
   SCSI_hABORTCHANNEL(hhcb, SCSI_HOST_ABT_BUS_RST);

   /* Reset channel software's parameters */
   SCSI_hRESETSOFTWARE(hhcb);

   /* Unblock upper layers from issuing Establish Connection HIOBs */
   SCSI_hTARGETHOLDESTHIOBS(hhcb,0);

   /* Now, restart sequencer */
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
  
   /* Save the mode. */ 
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Set MODE 3. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL0),
                 (OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL0)) & ~SCSI_SPIOEN));

   /* Restore the mode.  */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);

#endif /* (SCSI_TRIDENT_WORKAROUND && SCSI_TARGET_OPERATION) */

   /* OSM might disable interrupt and send down bus reset request before   */
   /* the chip gets interrupt.  If this happens, the chip is paused        */
   /* when CHIM enters SCSIhScsiBusReset() routine.                        */
   /* Since CHIM implementation is some way of reinitialization inside     */ 
   /* SCSIhScsiBusReset() routine, CHIM needs to just unpause the          */
   /* sequencer to restart the chip.                                       */

   /* UnPause the Sequencer */
   SCSIhUnPause(hhcb);
}
#endif /* (!SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */

/*********************************************************************
*
*  SCSIhTargetReset
*
*     Perform Target Reset or Logical Unit Reset request
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       This routine will be called when the target reset
*                 message or Logical Unit Reset message is ready to
*                 send to the target and during the
*                 sequencer interrupt CHIM to handle the AbortTarget.
*                 Therefore, the sequencer is already paused.
*                 
*                 This routine will abort an HIOB that
*                 1. belongs to the channel
*                 2. has same SCSI ID as Target Reset HIOB
*                 3. is not a Target Reset HIOB
*                 4. is currently in Target's Execution queue or
*                    pending at the target.
*
*********************************************************************/
#if (!SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
void SCSIhTargetReset (SCSI_HHCB SCSI_HPTR hhcb,
                       SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_HIOB SCSI_IPTR hiobAbort;
   SCSI_UEXACT16 i;
   SCSI_UEXACT8 haStatus;

   /*Setup status parameter for Target Reset or Logical Unit reset*/
   if (hiob->cmd==SCSI_CMD_LOGICAL_UNIT_RESET)
   {
      haStatus = SCSI_HOST_ABT_LUN_RST;
   }
   else
   {
      haStatus = SCSI_HOST_ABT_TRG_RST;
   }
   /* Check all HIOBs in the active pointer array for matching */
   /* target SCSI ID. If so, abort each HIOB separately. */
   for (i = 0; i < hhcb->numberScbs; i++)
   {
      if (((hiobAbort = SCSI_ACTPTR[i]) != SCSI_NULL_HIOB) &&
          (hiobAbort != hiob))
      {
#if SCSI_TARGET_OPERATION  
         if (hiob->SCSI_IP_targetMode ||
             hiob->cmd == SCSI_CMD_ESTABLISH_CONNECTION)
         {
            /* Target Reset has no effect on Target mode HIOBs */
            continue; 
         }    
         else
#endif /* SCSI_TARGET_OPERATION */
         /* Do not abort an HIOB does not belong to this channel */
         /* OR a Target Reset HIOB.                              */
         /* OR a LUN reset HIOB*/
         if ((SCSI_TARGET_UNIT(hiob)->hhcb != hhcb) ||
             (hiobAbort->cmd == SCSI_CMD_RESET_TARGET) ||
               (hiobAbort->cmd == SCSI_CMD_LOGICAL_UNIT_RESET))
         {
            continue;
         }      

         if ((SCSI_TARGET_UNIT(hiobAbort)->hhcb == hhcb) &&
             (SCSI_TARGET_UNIT(hiobAbort)->scsiID == SCSI_TARGET_UNIT(hiob)->scsiID))
         {
			/*If LUR then also check lunID*/
            if (hiob->cmd==SCSI_CMD_LOGICAL_UNIT_RESET)
            {
               if (SCSI_TARGET_UNIT(hiobAbort)->lunID != SCSI_TARGET_UNIT(hiob)->lunID)
               {
                  continue;
               }
            }
#if (SCSI_PACKETIZED_IO_SUPPORT)
            if (SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiob)->deviceTable))
            {
               /* For packetized mode, the aborted HIOB might be in        */
               /* sequencer Done queue, Done queue, or currently dis-      */
               /* connecting (pending at the target).  This logic will     */
               /* terminate the aborted HIOB that is pending at the target.*/
               /* Note, the aborted HIOB cannot be in:                     */
               /* 1. New queue because all regular IOBs after the Target   */
               /*    Reset IOB are queued in Device queue in RSM layer.    */
               /* 2. Target's Execution queue because all regular IOBs     */
               /*    before the Target Reset IOB should have already sent  */
               /*    to the target.                                        */
               if (!SCSIhStandardSearchDoneQ(hhcb, hiobAbort) &&
                   !SCSI_hSEARCHSEQDONEQ(hhcb, hiobAbort))
               {
                  /* Aborted HIOB must be disconnecting, abort it anyway. */
                  /* The target will not reconnect after Target Reset.    */

                  /* Clear Busy Target Table entry. */
                  SCSI_hTARGETCLEARBUSY(hhcb,hiobAbort);

                  hiobAbort->stat = SCSI_SCB_ABORTED;
                  hiobAbort->haStat = haStatus;
                  SCSIhTerminateCommand(hiobAbort);
               }
            }
            else
#endif /* (SCSI_PACKETIZED_IO_SUPPORT) */
            {
               /* For non-packetized mode, the aborted HIOB might be in      */
               /* sequencer Done queue, Done queue, Target's Execution queue */
               /* or currently disconnecting (pending at the target).  This  */
               /* logic will terminate the aborted HIOB that is in Target's  */
               /* Execution queue or pending at the target.                  */
               /* Note, the aborted HIOB cannot be in:                       */
               /* 1. New queue because all regular IOBs after the Target     */
               /*    Reset IOB are queued in Device queue in RSM layer.      */
               if (!SCSIhStandardSearchDoneQ(hhcb, hiobAbort) &&
                   !SCSI_hSEARCHSEQDONEQ(hhcb, hiobAbort) &&
                   !SCSI_hSEARCHEXEQ(hhcb, hiobAbort, haStatus, 1))
               {
                  /* Aborted HIOB must be disconnecting, abort it anyway. */
                  /* The target will not reconnect after Target Reset.    */

                  /* Clear Busy Target Table entry. */
                  SCSI_hTARGETCLEARBUSY(hhcb,hiobAbort);

                  hiobAbort->stat = SCSI_SCB_ABORTED;
                  hiobAbort->haStat = haStatus;
                  SCSIhTerminateCommand(hiobAbort);
               }
            }
         }
      }
   }  /* for */
}
#endif /* (!SCSI_DOWNSHIFT_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */

/*********************************************************************
*
*  SCSIhStandardAbortHIOB
*
*     Abort the passed-in HIOB for standard mode sequencer.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 aborted hiob
*                 host adapter status
*
*  Remarks:       This routine will check if the aborted HIOB is
*                 in Done queue.  And if not found, it will try
*                 to stop the sequencer by setting the sequencer
*                 breakpoint address at the idle loop for
*                 further searching and abort process handling.
*
*********************************************************************/
#if (SCSI_STANDARD_MODE && !SCSI_ASPI_SUPPORT_GROUP1)
void SCSIhStandardAbortHIOB (SCSI_HHCB SCSI_HPTR hhcb,
                             SCSI_HIOB SCSI_IPTR hiob,
                             SCSI_UEXACT8 haStatus)
{
   /* If aborted HIOB is in Done queue ... */
   if (!SCSIhStandardSearchDoneQ(hhcb, hiob))
   {
      /* The aborted HIOB is not in the Done queue.  So, it can be */
      /* in New queue, Execution queue, Sequencer Done queue or    */
      /* pending at the Target.  To get further the state of where */
      /* it might be, we need to stop the sequencer by setting the */
      /* sequencer breakpoint address at idle loop and check.      */

      /* Make sure we're not overwritten the host adapter status */
      /* because it might has been set while we're checking it.  */
      if (!hiob->haStat)
      {
         /* Set abort status */
         hiob->stat = SCSI_SCB_ABORTED;
         hiob->haStat = haStatus;
      }

      /* Set abort is in request to be used in breakpoint interrupt ISR */
      hiob->SCSI_IP_mgrStat = SCSI_MGR_ABORTINREQ;

      /* Set flag to indicate this is a non expander break interrupt */
      hhcb->SCSI_HP_nonExpBreak = 1;

      /* Stop the sequencer at idle_loop0. Normally, sequencer will */
      /* jump to it while waiting for other events.                 */
      SCSI_hSETBREAKPOINT(hhcb, (SCSI_UEXACT8)SCSI_ENTRY_IDLE_LOOP0_BREAK);
   }  /* end of if not in Done queue */
}
#endif /* (SCSI_STANDARD_MODE && !SCSI_ASPI_SUPPORT_GROUP1) */

/*********************************************************************
*
*  SCSIhStandardSearchSeqDoneQ
*
*     Search standard sequencer done queue for the aborted HIOB.
*
*  Return Value:  0 - aborted HIOB not found
*                 1 - aborted HIOB found
*
*  Parameters:    hhcb
*                 aborted hiob
*
*  Remarks:       Assumes sequencer is paused.
*
*********************************************************************/
#if !SCSI_DCH_U320_MODE
#if (SCSI_STANDARD_MODE && !SCSI_ASPI_SUPPORT_GROUP1)
SCSI_UEXACT8 SCSIhStandardSearchSeqDoneQ (SCSI_HHCB SCSI_HPTR hhcb,
                                          SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 currScb;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 retStat = 0;
   SCSI_UEXACT8 savedMode;

   /* Check if aborted HIOB is in sequencer done queue */

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 - a known mode for SCBPTR access. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
   /* Save mode 3 SCBPTR value. */
   scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
            ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);
   
   /* Get done head pointer */
   currScb = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_DONE_HEAD)) |
              ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_DONE_HEAD+1))) << 8);

   while (!SCSI_IS_NULL_SCB(currScb))
   {
      /* Set SCBPTR to currScb. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)currScb);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(currScb >> 8));

      if (currScb == hiob->scbNumber)
      {
         hiob->stat = 0;                        /* Clear HIOB's status */
         hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE; /* Indicate HIOB is completed */

         retStat = 1;
         break;
      }

      /* Next scb ram */
      currScb = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      SCSI_hQNEXTOFFSET(hhcb))) |
                 ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      SCSI_hQNEXTOFFSET(hhcb)+1))) << 8);
   }

   /* Restore mode 3 SCBPTR. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  

   return(retStat);
}
#endif /* (SCSI_STANDARD_MODE && !SCSI_ASPI_SUPPORT_GROUP1) */
#endif /* !SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhDchU320SearchSeqDoneQ
*
*     Search sequencer done queue for the aborting HIOB.
*
*  Return Value:  0 - aborting HIOB not found
*                 1 - aborting HIOB found
*
*  Parameters:    hhcb
*                 aborting hiob
*
*  Remarks:       Assumes sequencer is paused.
*
*********************************************************************/
#if SCSI_DCH_U320_MODE
SCSI_UEXACT8 SCSIhDchU320SearchSeqDoneQ (SCSI_HHCB SCSI_HPTR hhcb,
                                         SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 currScb;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 retStat = 0;
   SCSI_UEXACT8 savedMode;

   /* Check if aborting HIOB is in sequencer done queue */

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 - a known mode for SCBPTR access. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
   /* Save mode 3 SCBPTR value. */
   scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
            ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);
   
   /* Get done head pointer */
   currScb = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_HEAD)) |
              ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_DONE_HEAD+1))) << 8);

   while (!SCSI_IS_NULL_SCB(currScb))
   {
      /* Set SCBPTR to currScb. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)currScb);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(currScb >> 8));

      if (currScb == hiob->scbNumber)
      {
         hiob->stat = 0;                        /* Clear HIOB's status */
         hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE; /* Indicate HIOB is completed */

         retStat = 1;
         break;
      }
      
      /* Next scb ram */
      currScb = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next))) |
                 ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1))) << 8);
   }

   /* Restore mode 3 SCBPTR. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  

   return(retStat);
}
#endif /* SCSI_DCH_U320_MODE */
/*********************************************************************
*
*  SCSIhStandardSearchDoneQ
*
*     Search Done queue for the aborted HIOB for standard mode
*
*  Return Value:  0 - aborted HIOB not found
*                 1 - aborted HIOB found
*
*  Parameters:    hhcb
*                 aborted hiob
*
*  Remarks:
*
*********************************************************************/
#if (SCSI_STANDARD_MODE && !SCSI_ASPI_SUPPORT_GROUP1)
SCSI_UEXACT8 SCSIhStandardSearchDoneQ (SCSI_HHCB SCSI_HPTR hhcb,
                                       SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_QOUTFIFO_NEW SCSI_HPTR qOutPtr;
   SCSI_UEXACT16 scbNumber;
   SCSI_UEXACT16 qDoneElement;
   SCSI_UEXACT8 qPassCnt;
   SCSI_UEXACT8 qDonePass;
   SCSI_UEXACT8 retStat = 0;

   /* Check if aborting HIOB is in Done queue */
   qDoneElement = hhcb->SCSI_HP_qDoneElement;
   qOutPtr = SCSI_QOUT_PTR_ARRAY_NEW(hhcb) + qDoneElement;

   /* Invalidate the whole QOUTFIFO */
   SCSI_INVALIDATE_CACHE((SCSI_UEXACT8 SCSI_HPTR)SCSI_QOUT_PTR_ARRAY_NEW(hhcb),
        hhcb->numberScbs * sizeof(SCSI_QOUTFIFO_NEW));

   SCSI_GET_LITTLE_ENDIAN8(hhcb, &qPassCnt, qOutPtr,
        OSDoffsetof(SCSI_QOUTFIFO_NEW, quePassCnt));

   qDonePass = hhcb->SCSI_HP_qDonePass;

   /* Keep on searching until Pass Count and Done Pass value */
   /* are different or the aborted HIOB found. */
   while (qPassCnt == qDonePass)
   {
      /* Found valid entry, get scb number */
      SCSI_GET_LITTLE_ENDIAN16(hhcb, &scbNumber, qOutPtr,
           OSDoffsetof(SCSI_QOUTFIFO_NEW, scbNumber));

      /* Check if scb number matches with aborted HIOB */
      if (scbNumber == hiob->scbNumber)
      {
         /* Make sure HIOB's status is clear. Just in case, */
         /* the completed HIOB is found during active abort. */
         hiob->stat = 0;

         /*  Set the aborted HIOB was finished prior the abort requested. */
         hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE;

         retStat = 1;
         break;
      }

      /* Increment qDoneElement */
      if (++qDoneElement == hhcb->totalDoneQElements)
      {
         qDoneElement = 0;
         ++qDonePass;
      }

      qOutPtr = SCSI_QOUT_PTR_ARRAY_NEW(hhcb) + qDoneElement;

      SCSI_GET_LITTLE_ENDIAN8(hhcb, &qPassCnt, qOutPtr,
           OSDoffsetof(SCSI_QOUTFIFO_NEW, quePassCnt));
   }

   /* Flush the whole QOUTFIFO */
   SCSI_FLUSH_CACHE((SCSI_UEXACT8 SCSI_HPTR)SCSI_QOUT_PTR_ARRAY_NEW(hhcb),
        hhcb->numberScbs * sizeof(SCSI_QOUTFIFO_NEW));

   return(retStat);
}
#endif /* (SCSI_STANDARD_MODE && !SCSI_ASPI_SUPPORT_GROUP1) */

/*********************************************************************
*
*  SCSIhStandardSearchNewQ
*
*     Search New queue for the aborted HIOB.
*
*  Return Value:  0 - aborted HIOB not found
*                 1 - aborted HIOB found
*
*  Parameters:    hhcb
*                 aborted hiob
*                 host adapter status
*
*  Remarks:
*
*********************************************************************/
#if (SCSI_STANDARD_MODE && !SCSI_ASPI_SUPPORT_GROUP1)
SCSI_UEXACT8 SCSIhStandardSearchNewQ (SCSI_HHCB SCSI_HPTR hhcb,
                                      SCSI_HIOB SCSI_IPTR hiob,
                                      SCSI_UEXACT8 haStatus)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 scbNumber;
   SCSI_UEXACT16 scbCurr;
   SCSI_UEXACT16 scbPrev;
   SCSI_BUS_ADDRESS scbBusAddress;
   SCSI_SCB_DESCRIPTOR SCSI_HPTR scbDescriptor;
   SCSI_HIOB SCSI_IPTR hiobCurr = SCSI_NULL_HIOB;
   SCSI_HIOB SCSI_IPTR hiobPrev = SCSI_NULL_HIOB;
   SCSI_UEXACT8 retStat = 0;


   /* Obtain scbnumber sitting at the head of the new queue and its        */
   /* corresponding hiob.  To get the scbNumber we need to get the         */
   /* q_new_pointer first and then search the active pointer array to get  */
   /* the hiob containing the same address.                                */
   scbBusAddress = SCSI_hGETADDRESSSCRATCH(hhcb,SCSI_hQ_NEW_POINTER(hhcb));

   for (scbNumber = 0; scbNumber < hhcb->numberScbs; scbNumber++)
   {
      /* Obtain HIOB in Active pointer array */ 
      hiobCurr = SCSI_ACTPTR[scbNumber];

      if (hiobCurr != SCSI_NULL_HIOB)
      {
         if (!SCSIhCompareBusAddress(hiobCurr->scbDescriptor->scbBuffer.busAddress,
             scbBusAddress))
         {
            break;
         }
         else if (scbNumber == (hhcb->numberScbs-1))
         {
            hiobCurr = SCSI_NULL_HIOB;
         }
      }
   }

   scbCurr = scbPrev = scbNumber;

   /* Have the search completed? */
   while (hiobCurr != SCSI_NULL_HIOB)
   {
      if (hiobCurr == hiob)
      {
         /* Set abort status */
         hiob->stat = SCSI_SCB_ABORTED;
         hiob->haStat = haStatus;

         /* Set abort request is done */
         hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE_ABT;

         /* We won't remove the aborted HIOB if it lies at the head of */
         /* the New queue or next to the head because sequencer might  */
         /* accessing it. If it is a bus_device_reset, then post back  */
         /* the HIOB.                                                  */
         if ((scbCurr == scbNumber) || (scbPrev == scbNumber))
         {
            SCSI_hUPDATEABORTBITHOSTMEM(hhcb,hiob);
         }
         else
         {
            /* Decrement and update host new SCB queue offset register */
            hhcb->SCSI_HP_hnscbQoff--;
            OSD_OUTEXACT16(SCSI_AICREG(SCSI_HNSCB_QOFF), hhcb->SCSI_HP_hnscbQoff);

            /* Remove aborted HIOB from New queue and mend the queue */
            /* Obtain previous HIOB in Active pointer array */ 
            hiobPrev = SCSI_ACTPTR[scbPrev];
            SCSI_hUPDATENEXTSCBADDRESS(hhcb,hiobPrev,
                  hiobCurr->scbDescriptor->queueNext->scbBuffer.busAddress);

            /* The scbDescriptor->queueNext also needs to be mended  */ 
            /* since we use it to search elements in new queue now   */
            hiobPrev->scbDescriptor->queueNext = hiobCurr->scbDescriptor->queueNext;

            /* Post aborted HIOB back to the upper layer */
            SCSI_hTERMINATECOMMAND(hiob);
         }

         retStat = 1;
         break;
      }

      scbPrev = scbCurr;
      /* .....Obtain the scbNumber */   
      scbDescriptor = hiobCurr->scbDescriptor->queueNext;
      scbCurr = scbDescriptor->scbNumber;

      /* We reach the end of the new queue if the current scb is NULL. */
      /* So, just exit the search. */
      if (SCSI_IS_NULL_SCB(scbCurr))
      {
         break;
      }
      else
      {
         /* ... obtain HIOB in Active pointer array */
         hiobCurr = SCSI_ACTPTR[scbCurr];
      }
   }

   return(retStat);
}
#endif /* (SCSI_STANDARD_MODE && !SCSI_ASPI_SUPPORT_GROUP1) */

/*********************************************************************
*
*   SCSIhStandardU320UpdateNextScbAddress
*
*     Modify next SCB address in the SCB for standard U320 mode 
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*                 ScbBusAddress
*
*  Remarks:
*
*********************************************************************/
#if SCSI_STANDARD_U320_MODE
void SCSIhStandardU320UpdateNextScbAddress (SCSI_HHCB SCSI_HPTR hhcb,
                                            SCSI_HIOB SCSI_IPTR hiob,
                                            SCSI_BUS_ADDRESS ScbBusAddress)
{
   SCSI_SCB_STANDARD_U320 SCSI_IPTR scbBuffer =
      (SCSI_SCB_STANDARD_U320 SCSI_IPTR) hiob->scbDescriptor->scbBuffer.virtualAddress;

   SCSI_hPUTBUSADDRESS(hhcb, scbBuffer,
        OSDoffsetof(SCSI_SCB_STANDARD_U320, SCSI_SU320_next_SCB_addr0), ScbBusAddress);

   /* Flush the cache */
   SCSI_FLUSH_CACHE(scbBuffer, sizeof(SCSI_SCB_STANDARD_U320));
}
#endif /* SCSI_STANDARD_U320_MODE */

/*********************************************************************
*
*   SCSIhStandardEnhU320UpdateNextScbAddress
*
*     Modify next SCB address in the SCB for standard enhanced U320 mode 
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*                 ScbBusAddress
*
*  Remarks:
*
*********************************************************************/
#if SCSI_STANDARD_ENHANCED_U320_MODE
void SCSIhStandardEnhU320UpdateNextScbAddress (SCSI_HHCB SCSI_HPTR hhcb,
                                               SCSI_HIOB SCSI_IPTR hiob,
                                               SCSI_BUS_ADDRESS ScbBusAddress)
{
   SCSI_SCB_STANDARD_ENH_U320 SCSI_IPTR scbBuffer =
      (SCSI_SCB_STANDARD_ENH_U320 SCSI_IPTR) hiob->scbDescriptor->scbBuffer.virtualAddress;

   SCSI_hPUTBUSADDRESS(hhcb, scbBuffer,
        OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320, SCSI_SEU320_next_SCB_addr0), ScbBusAddress);

   /* Flush the cache */
   SCSI_FLUSH_CACHE(scbBuffer, sizeof(SCSI_SCB_STANDARD_ENH_U320));
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/*********************************************************************
*
*   SCSIhDchU320UpdateNextScbAddress
*
*     Modify next SCB address in the SCB for standard U320 mode 
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*                 ScbBusAddress
*
*  Remarks:
*
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320UpdateNextScbAddress (SCSI_HHCB SCSI_HPTR hhcb,
                                       SCSI_HIOB SCSI_IPTR hiob,
                                       SCSI_BUS_ADDRESS ScbBusAddress)
{
   SCSI_SCB_DCH_U320 SCSI_IPTR scbBuffer =
      (SCSI_SCB_DCH_U320 SCSI_IPTR) hiob->scbDescriptor->scbBuffer.virtualAddress;

   SCSI_hPUTBUSADDRESS(hhcb, scbBuffer,
        OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_next_SCB_addr0), ScbBusAddress);

   /* Flush the cache */
   SCSI_FLUSH_CACHE(scbBuffer, sizeof(SCSI_SCB_DCH_U320));
}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardSearchExeQ
*
*     Search standard Execution queue for the aborted HIOB.
*
*  Return Value:  0 - aborted HIOB not found
*                 1 - aborted HIOB found
*
*  Parameters:    hhcb
*                 aborted hiob
*                 host adapter status
*                 post HIOB back if found flag:
*                    1 - yes
*                    0 - no
*
*  Remarks:       Assumes sequencer is paused.
*
*********************************************************************/
#if (SCSI_STANDARD_MODE && (!SCSI_DCH_U320_MODE) && !(SCSI_ASPI_SUPPORT_GROUP1))
SCSI_UEXACT8 SCSIhStandardSearchExeQ (SCSI_HHCB SCSI_HPTR hhcb,
                                      SCSI_HIOB SCSI_IPTR hiob,
                                      SCSI_UEXACT8 haStatus,
                                      SCSI_UEXACT8 postHiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_HIOB SCSI_IPTR hiobSelecting = SCSI_NULL_HIOB;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT16 scbNumber;
   SCSI_UEXACT16 prevScb;
   SCSI_UEXACT16 nextScbTargQ;
   SCSI_UEXACT16 nextScbComQ;
   SCSI_UEXACT16 targTail;
   SCSI_UEXACT16 comQTail;
   SCSI_UEXACT8 retStat = 0;
   SCSI_UEXACT8 savedMode;
   SCSI_UEXACT8 targetID;
   SCSI_UEXACT16 qExeTargNextScbArray;
   SCSI_UEXACT16 qNextScbArray;

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Switch to mode 3 - a known mode for SCBPTR access. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Save mode 3 SCBPTR value. */
   scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
            ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

   /* Get the target id */
   targetID = SCSI_TARGET_UNIT(hiob)->scsiID;

   targTail =
      (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*targetID)) |
       ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*targetID+1))) << 8);

   /* Check if aborted HIOB is in execution queue */
   if (!SCSI_IS_NULL_SCB(targTail))
   {
      /* Assign the register mapped SCB field (SCBARRAY) variables. */  
      qExeTargNextScbArray =
         (SCSI_UEXACT16)(SCSI_SCB_BASE + SCSI_hQEXETARGNEXTOFFSET(hhcb));
      qNextScbArray =
         (SCSI_UEXACT16)(SCSI_SCB_BASE + SCSI_hQNEXTOFFSET(hhcb));

      scbNumber =
         ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
         (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));

      /* Get the HIOB that was attempting to perform selection and */
      /* check if its target ID matches with the aborted HIOB.     */
      if (!SCSI_IS_NULL_SCB(scbNumber))
      {
         hiobSelecting = SCSI_ACTPTR[scbNumber];
      }

#if SCSI_TARGET_OPERATION
      if (hiobSelecting->SCSI_IP_targetMode)
      {
         /* Target mode request, therefore no need to check if
          * to same device.
          * Set hiobSelection to NULL to take the else path. 
          */
         hiobSelecting = SCSI_NULL_HIOB;
      }
#endif /* SCSI_TARGET_OPERATION */

      /* If the head SCB in the specific target's execution queue */
      /* is selecting, then we will search through the queue for  */
      /* the aborted HIOB. */
#if SCSI_PACKETIZED_IO_SUPPORT
      if ((hiobSelecting != SCSI_NULL_HIOB) &&
          (SCSI_TARGET_UNIT(hiobSelecting)->hhcb == hhcb) &&
          (!SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiobSelecting)->deviceTable)) &&
          ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) ||
           (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & SCSI_SELDO)) &&
          (OSD_INEXACT8(SCSI_AICREG(SCSI_SELOID)) == targetID))
#else
      if ((hiobSelecting != SCSI_NULL_HIOB) &&
          (SCSI_TARGET_UNIT(hiobSelecting)->hhcb == hhcb) &&
          ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) ||
           (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & SCSI_SELDO)) &&
          (OSD_INEXACT8(SCSI_AICREG(SCSI_SELOID)) == targetID))
#endif /* (SCSI_PACKETIZED_IO_SUPPORT) */
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbNumber >> 8));

         prevScb = scbNumber;

         scbNumber =
            (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray)) |
            ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(
                                                  qExeTargNextScbArray+1))) << 8);

         while (!SCSI_IS_NULL_SCB(scbNumber))
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbNumber);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbNumber >> 8));

            if (scbNumber == hiob->scbNumber)
            {
               /* Set abort status */
               hiob->stat = SCSI_SCB_ABORTED;
               hiob->haStat = haStatus;

               /* Set abort request is done */
               hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE_ABT;

               /* Remove the SCB from target's execution queue */
               nextScbTargQ =
                  (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray)) |
                     ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(
                                                           qExeTargNextScbArray+1))) << 8);

               /* Mending the target's execution queue */
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)prevScb);
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(prevScb >> 8));

               OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray),
                                  (SCSI_UEXACT8)nextScbTargQ);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray+1),
                                  (SCSI_UEXACT8)(nextScbTargQ >> 8));

               if (scbNumber == targTail)
               {
                  /* Aborted HIOB at the target's execution queue tail. */
                  /* Need to update the entry in q_exetarg_tail table.  */
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*targetID),
                      (SCSI_UEXACT8)prevScb);
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*targetID+1),
                      (SCSI_UEXACT8)(prevScb >> 8));
               }

               if (postHiob)
               {
                  /* Post it back to upper layer as instructed */
                  SCSIhTerminateCommand(hiob);
               }

               retStat = 1;
               break;
            }

            /* Keep previous SCB */
            prevScb = scbNumber;

            /* Advance to next SCB in target Execution queue */
            scbNumber =
               (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray)) |
                ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(
                                                      qExeTargNextScbArray+1))) << 8);
         }
      }
      else
      {
         /* There is no SCB in selecting process, then we will search */
         /* through the common execution queue and individual target  */
         /* execution queue for the aborted HIOB. */
         SCSI_SET_NULL_SCB(prevScb);
         scbNumber =
            (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD)) |
           ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1))) << 8);

         while (!SCSI_IS_NULL_SCB(scbNumber))
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbNumber);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbNumber >> 8));

            /* Aborted SCB is at the head of Target's Execution queue? */
            if (scbNumber == hiob->scbNumber)
            {
               /* Set abort status */
               hiob->stat = SCSI_SCB_ABORTED;
               hiob->haStat = haStatus;

               /* Set abort request is done */
               hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE_ABT;

               /* Remove the SCB from common execution queue */
               nextScbTargQ =
                  (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray)) |
                   ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(
                                                         qExeTargNextScbArray+1))) << 8);

               nextScbComQ =
                  (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qNextScbArray)) |
                   ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1))) << 8);

               /* Mending the common execution queue */

               /* Aborted HIOB at the common exe queue head if prevScb is NULL*/
               if (SCSI_IS_NULL_SCB(prevScb))
               {
                  if (scbNumber == targTail)
                  {
                     /* Aborted HIOB is at the target's exe queue tail. */
                     /* Need to update the common exe queue head entry  */
                     /* to point to the next SCB in common exe queue.   */
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD),
                                        (SCSI_UEXACT8)nextScbComQ);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1),
                                        (SCSI_UEXACT8)(nextScbComQ >> 8));
                  }
                  else
                  {
                     /* Aborted HIOB is not at the target's exe queue tail. */
                     /* Need to update the common exe queue head entry to   */
                     /* point to the next SCB in target's exe queue.        */
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD),
                                        (SCSI_UEXACT8)nextScbTargQ);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1),
                                        (SCSI_UEXACT8)(nextScbTargQ >> 8));
                  }
               }
               else
               {
                  /* Aborted HIOB is not at the common exe queue head */
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)prevScb);
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(prevScb >> 8));

                  if (scbNumber == targTail)
                  {
                     /* Aborted HIOB is at the target's exe queue tail. */
                     /* Need to update common exe queue to make the     */
                     /* previous SCB point to the next SCB.             */
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray),
                                        (SCSI_UEXACT8)nextScbComQ);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1),
                                        (SCSI_UEXACT8)(nextScbComQ >> 8));
                  }
                  else
                  {
                     /* Aborted HIOB is not at the target's exe queue tail. */
                     /* Need to update the previous SCB in common exe queue */
                     /* to point to the next SCB in target's exe queue.     */
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray),
                                        (SCSI_UEXACT8)nextScbTargQ);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1),
                                        (SCSI_UEXACT8)(nextScbTargQ >> 8));
                  }
               } /* if (SCSI_IS_NULL_SCB(prevScb)) */

               comQTail = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL)) |
                          ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL+1)) << 8);

               if (scbNumber == targTail)
               {
                  /* Clear the entry in q_exetarg_tail table */
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*targetID+1),
                                     (SCSI_UEXACT8)(SCSI_NULL_SCB));

                  /* Update common exe queue tail entry to the prevScb */
                  /* if the CEQ tail points to the aborted HIOB.       */
                  if (scbNumber == comQTail)
                  {
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL),
                                        (SCSI_UEXACT8)prevScb);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL+1),
                                        (SCSI_UEXACT8)(prevScb >> 8));
                  }
               }
               else
               {
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)nextScbTargQ);
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(nextScbTargQ >> 8));

                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray),
                                     (SCSI_UEXACT8)nextScbComQ);
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1),
                                     (SCSI_UEXACT8)(nextScbComQ >> 8));

                  /* Update common exe queue tail entry to the nextScbTargQ */
                  /* if the CEQ tail points to the aborted HIOB.            */
                  if (scbNumber == comQTail)
                  {
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL),
                                        (SCSI_UEXACT8)nextScbTargQ);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL+1),
                                        (SCSI_UEXACT8)(nextScbTargQ >> 8));
                  }
               }

               if (postHiob)
               {
                  /* Post it back to upper layer as instructed */
                  SCSIhTerminateCommand(hiob);
               }

               retStat = 1;
               break;
            }
            else
            if ((SCSI_ACTPTR[scbNumber] != SCSI_NULL_HIOB) &&
                (SCSI_TARGET_UNIT(SCSI_ACTPTR[scbNumber])->scsiID == targetID))
            {
               /* Search through target's exe queue for the aborted hiob */
               prevScb = scbNumber;
               scbNumber =
                  (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray)) |
                 ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(
                                                       qExeTargNextScbArray+1))) << 8);

               while (!SCSI_IS_NULL_SCB(scbNumber))
               {
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbNumber);
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbNumber >> 8));

                  if (scbNumber == hiob->scbNumber)
                  {
                     /* Set abort status */
                     hiob->stat = SCSI_SCB_ABORTED;
                     hiob->haStat = haStatus;

                     /* Set abort request is done */
                     hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE_ABT;

                     /* Remove the SCB from target's execution queue */
                     nextScbTargQ =
                        (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray)) |
                          ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(
                                                                qExeTargNextScbArray+1))) << 8);

                     /* Mending the target's execution queue */
                     OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)prevScb);
                     OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(prevScb >> 8));

                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray),
                                        (SCSI_UEXACT8)nextScbTargQ);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray+1),
                                        (SCSI_UEXACT8)(nextScbTargQ >> 8));

                     if (scbNumber == targTail)
                     {
                        /* Removed HIOB at the target's execution queue tail. */
                        /* Need to update the entry in q_exetarg_tail table.  */
                        OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*targetID),
                            (SCSI_UEXACT8)prevScb);
                        OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*targetID+1),
                            (SCSI_UEXACT8)(prevScb >> 8));
                     }

                     if (postHiob)
                     {
                        /* Post it back to upper layer as instructed */
                        SCSIhTerminateCommand(hiob);
                     }

                     retStat = 1;
                     break;
                  }

                  /* Keep previous SCB */
                  prevScb = scbNumber;

                  /* Advance to next SCB in target Execution queue */
                  scbNumber =
                     (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray)) |
                      ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(
                                                            qExeTargNextScbArray+1))) << 8);
               } /* while (!SCSI_IS_NULL_SCB(scbNumber)) */

               /* break out the outside while loop since aborting hiob found */
               break;
            }

            /* Keep previous SCB */
            prevScb = scbNumber;

            /* Advance to next SCB in common Execution queue */
            scbNumber =
               (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qNextScbArray)) |
                 ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1))) << 8);
         }
      }
   }

   /* Restore mode 3 SCBPTR. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  

   return(retStat);
}
#endif /* (SCSI_STANDARD_MODE && (!SCSI_DCH_U320_MODE) && !(SCSI_ASPI_SUPPORT_GROUP1)) */

/*********************************************************************
*
*  SCSIhDchU320SearchExeQ
*
*     Search Execution queue for the aborted HIOB.
*
*  Return Value:  0 - aborted HIOB not found
*                 1 - aborted HIOB found
*
*  Parameters:    hhcb
*                 aborted hiob
*                 host adapter status
*                 post HIOB back if found flag:
*                    1 - yes
*                    0 - no
*
*  Remarks:       Assumes sequencer is paused.
*
*********************************************************************/
#if SCSI_DCH_U320_MODE
SCSI_UEXACT8 SCSIhDchU320SearchExeQ (SCSI_HHCB SCSI_HPTR hhcb,
                                     SCSI_HIOB SCSI_IPTR hiob,
                                     SCSI_UEXACT8 haStatus,
                                     SCSI_UEXACT8 postHiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_HIOB SCSI_IPTR hiobSelecting = SCSI_NULL_HIOB;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT16 scbNumber;
   SCSI_UEXACT16 prevScb;
   SCSI_UEXACT16 nextScbTargQ;
   SCSI_UEXACT16 nextScbComQ;
   SCSI_UEXACT16 targTail;
   SCSI_UEXACT16 comQTail;
   SCSI_UEXACT8 retStat = 0;
   SCSI_UEXACT8 savedMode;
   SCSI_UEXACT8 targetID;

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Switch to mode 3 - a known mode for SCBPTR access. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Save mode 3 SCBPTR value. */
   scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
            ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

   /* Get the target id */
   targetID = SCSI_TARGET_UNIT(hiob)->scsiID;

   targTail =
      (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*targetID)) |
       ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*targetID+1))) << 8);

   /* Check if aborted HIOB is in execution queue */
   if (!SCSI_IS_NULL_SCB(targTail))
   {
      scbNumber =
         ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
         (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));

      /* Get the HIOB that was attempting to perform selection and */
      /* check if its target ID matches with the aborted HIOB.     */
      if (!SCSI_IS_NULL_SCB(scbNumber))
      {
         hiobSelecting = SCSI_ACTPTR[scbNumber];
      }

#if SCSI_TARGET_OPERATION
      if (hiobSelecting->SCSI_IP_targetMode)
      {
         /* Target mode request, therefore no need to check if
          * to same device.
          * Set hiobSelection to NULL to take the else path. 
          */
         hiobSelecting = SCSI_NULL_HIOB;
      }
#endif /* SCSI_TARGET_OPERATION */
      
      /* If the head SCB in the specific target's execution queue */
      /* is selecting, then we will search through the queue for  */
      /* the aborted HIOB. */
#if SCSI_PACKETIZED_IO_SUPPORT
      if ((hiobSelecting != SCSI_NULL_HIOB) &&
          (SCSI_TARGET_UNIT(hiobSelecting)->hhcb == hhcb) &&
          (!SCSI_hISPACKETIZED(SCSI_TARGET_UNIT(hiobSelecting)->deviceTable)) &&
          ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) ||
           (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & SCSI_SELDO)) &&
          (OSD_INEXACT8(SCSI_AICREG(SCSI_SELOID)) == targetID))
#else
      if ((hiobSelecting != SCSI_NULL_HIOB) &&
          (SCSI_TARGET_UNIT(hiobSelecting)->hhcb == hhcb) &&
          ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)) & SCSI_ENSELO) ||
           (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT0)) & SCSI_SELDO)) &&
          (OSD_INEXACT8(SCSI_AICREG(SCSI_SELOID)) == targetID))
#endif /* (SCSI_PACKETIZED_IO_SUPPORT) */
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbNumber);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbNumber >> 8));

         prevScb = scbNumber;

         scbNumber =
            (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
               OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next))) |
            ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
               OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)+1))) << 8);

         while (!SCSI_IS_NULL_SCB(scbNumber))
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbNumber);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbNumber >> 8));

            if (scbNumber == hiob->scbNumber)
            {
               /* Set abort status */
               hiob->stat = SCSI_SCB_ABORTED;
               hiob->haStat = haStatus;

               /* Set abort request is done */
               hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE_ABT;

               /* Remove the SCB from target's execution queue */
               nextScbTargQ = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                 OSDoffsetof(SCSI_SCB_DCH_U320,
                                             SCSI_DCHU320_q_exetarg_next))) |
                             ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                 OSDoffsetof(SCSI_SCB_DCH_U320,
                                             SCSI_DCHU320_q_exetarg_next)+1))) << 8);

               /* Mending the target's execution queue */
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)prevScb);
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(prevScb >> 8));

               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                   OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)),
                   (SCSI_UEXACT8)nextScbTargQ);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                   OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)+1),
                   (SCSI_UEXACT8)(nextScbTargQ >> 8));

               if (scbNumber == targTail)
               {
                  /* Aborted HIOB at the target's execution queue tail. */
                  /* Need to update the entry in q_exetarg_tail table.  */
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*targetID),
                      (SCSI_UEXACT8)prevScb);
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*targetID+1),
                      (SCSI_UEXACT8)(prevScb >> 8));
               }

               if (postHiob)
               {
                  /* Post it back to upper layer as instructed */
                  SCSIhTerminateCommand(hiob);
               }

               retStat = 1;
               break;
            }

            /* Keep previous SCB */
            prevScb = scbNumber;

            /* Advance to next SCB in target Execution queue */
            scbNumber = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                           OSDoffsetof(SCSI_SCB_DCH_U320,
                                       SCSI_DCHU320_q_exetarg_next))) |
                       ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                           OSDoffsetof(SCSI_SCB_DCH_U320,
                                       SCSI_DCHU320_q_exetarg_next)+1))) << 8);
         }
      }
      else
      {
         /* There is no SCB in selecting process, then we will search */
         /* through the common execution queue and individual target  */
         /* execution queue for the aborted HIOB. */
         SCSI_SET_NULL_SCB(prevScb);
         scbNumber =
            (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD)) |
           ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1))) << 8);

         while (!SCSI_IS_NULL_SCB(scbNumber))
         {
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbNumber);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbNumber >> 8));

            /* Aborted SCB is at the head of Target's Execution queue? */
            if (scbNumber == hiob->scbNumber)
            {
               /* Set abort status */
               hiob->stat = SCSI_SCB_ABORTED;
               hiob->haStat = haStatus;

               /* Set abort request is done */
               hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE_ABT;

               /* Remove the SCB from common execution queue */
               nextScbTargQ = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                 OSDoffsetof(SCSI_SCB_DCH_U320,
                                             SCSI_DCHU320_q_exetarg_next))) |
                             ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                 OSDoffsetof(SCSI_SCB_DCH_U320,
                                             SCSI_DCHU320_q_exetarg_next)+1))) << 8);

               nextScbComQ = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                 OSDoffsetof(SCSI_SCB_DCH_U320,
                                             SCSI_DCHU320_q_next))) |
                            ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                 OSDoffsetof(SCSI_SCB_DCH_U320,
                                             SCSI_DCHU320_q_next)+1))) << 8);

               /* Mending the common execution queue */

               /* Aborted HIOB at the common exe queue head if prevScb is NULL*/
               if (SCSI_IS_NULL_SCB(prevScb))
               {
                  if (scbNumber == targTail)
                  {
                     /* Aborted HIOB is at the target's exe queue tail. */
                     /* Need to update the common exe queue head entry  */
                     /* to point to the next SCB in common exe queue.   */
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD),
                                        (SCSI_UEXACT8)nextScbComQ);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1),
                                        (SCSI_UEXACT8)(nextScbComQ >> 8));
                  }
                  else
                  {
                     /* Aborted HIOB is not at the target's exe queue tail. */
                     /* Need to update the common exe queue head entry to   */
                     /* point to the next SCB in target's exe queue.        */
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD),
                                        (SCSI_UEXACT8)nextScbTargQ);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1),
                                        (SCSI_UEXACT8)(nextScbTargQ >> 8));
                  }
               }
               else
               {
                  /* Aborted HIOB is not at the common exe queue head */
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)prevScb);
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(prevScb >> 8));

                  if (scbNumber == targTail)
                  {
                     /* Aborted HIOB is at the target's exe queue tail. */
                     /* Need to update common exe queue to make the     */
                     /* previous SCB point to the next SCB.             */
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                        OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)),
                        (SCSI_UEXACT8)nextScbComQ);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                        OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1),
                        (SCSI_UEXACT8)(nextScbComQ >> 8));
                  }
                  else
                  {
                     /* Aborted HIOB is not at the target's exe queue tail. */
                     /* Need to update the previous SCB in common exe queue */
                     /* to point to the next SCB in target's exe queue.     */
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                        OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)),
                        (SCSI_UEXACT8)nextScbTargQ);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                        OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1),
                        (SCSI_UEXACT8)(nextScbTargQ >> 8));
                  }
               } /* if (SCSI_IS_NULL_SCB(prevScb)) */

               comQTail = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL)) |
                          ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL+1)) << 8);

               if (scbNumber == targTail)
               {
                  /* Clear the entry in q_exetarg_tail table */
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*targetID+1),
                                     (SCSI_UEXACT8)(SCSI_NULL_SCB));

                  /* Update common exe queue tail entry to the prevScb */
                  /* if the CEQ tail points to the aborted HIOB.       */
                  if (scbNumber == comQTail)
                  {
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL),
                                        (SCSI_UEXACT8)prevScb);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL+1),
                                        (SCSI_UEXACT8)(prevScb >> 8));
                  }
               }
               else
               {
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)nextScbTargQ);
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(nextScbTargQ >> 8));

                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)),
                     (SCSI_UEXACT8)nextScbComQ);
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1),
                     (SCSI_UEXACT8)(nextScbComQ >> 8));

                  /* Update common exe queue tail entry to the nextScbTargQ */
                  /* if the CEQ tail points to the aborted HIOB.            */
                  if (scbNumber == comQTail)
                  {
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL),
                                        (SCSI_UEXACT8)nextScbTargQ);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL+1),
                                        (SCSI_UEXACT8)(nextScbTargQ >> 8));
                  }
               }

               if (postHiob)
               {
                  /* Post it back to upper layer as instructed */
                  SCSIhTerminateCommand(hiob);
               }

               retStat = 1;
               break;
            }
            else
            if ((SCSI_ACTPTR[scbNumber] != SCSI_NULL_HIOB) &&
                (SCSI_TARGET_UNIT(SCSI_ACTPTR[scbNumber])->scsiID == targetID))
            {
               /* Search through target's exe queue for the aborted hiob */
               prevScb = scbNumber;
               scbNumber =
                  (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next))) |
                 ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)+1))) << 8);

               while (!SCSI_IS_NULL_SCB(scbNumber))
               {
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbNumber);
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbNumber >> 8));

                  if (scbNumber == hiob->scbNumber)
                  {
                     /* Set abort status */
                     hiob->stat = SCSI_SCB_ABORTED;
                     hiob->haStat = haStatus;

                     /* Set abort request is done */
                     hiob->SCSI_IP_mgrStat = SCSI_MGR_DONE_ABT;

                     /* Remove the SCB from target's execution queue */
                     nextScbTargQ = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                       OSDoffsetof(SCSI_SCB_DCH_U320,
                                                   SCSI_DCHU320_q_exetarg_next))) |
                                   ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                       OSDoffsetof(SCSI_SCB_DCH_U320,
                                                   SCSI_DCHU320_q_exetarg_next)+1))) << 8);

                     /* Mending the target's execution queue */
                     OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)prevScb);
                     OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(prevScb >> 8));

                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                        OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)),
                        (SCSI_UEXACT8)nextScbTargQ);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                        OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)+1),
                        (SCSI_UEXACT8)(nextScbTargQ >> 8));

                     if (scbNumber == targTail)
                     {
                        /* Removed HIOB at the target's execution queue tail. */
                        /* Need to update the entry in q_exetarg_tail table.  */
                        OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*targetID),
                            (SCSI_UEXACT8)prevScb);
                        OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*targetID+1),
                            (SCSI_UEXACT8)(prevScb >> 8));
                     }

                     if (postHiob)
                     {
                        /* Post it back to upper layer as instructed */
                        SCSIhTerminateCommand(hiob);
                     }

                     retStat = 1;
                     break;
                  }

                  /* Keep previous SCB */
                  prevScb = scbNumber;

                  /* Advance to next SCB in target Execution queue */
                  scbNumber = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                 OSDoffsetof(SCSI_SCB_DCH_U320,
                                             SCSI_DCHU320_q_exetarg_next))) |
                             ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                                 OSDoffsetof(SCSI_SCB_DCH_U320,
                                             SCSI_DCHU320_q_exetarg_next)+1))) << 8);
               } /* while (!SCSI_IS_NULL_SCB(scbNumber)) */

               /* break out the outside while loop since aborting hiob found */
               break;
            }

            /* Keep previous SCB */
            prevScb = scbNumber;

            /* Advance to next SCB in common Execution queue */
            scbNumber = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                           OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next))) |
                       ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                           OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1))) << 8);
         }
      }
   }

   /* Restore mode 3 SCBPTR. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  

   return(retStat);
}
#endif /* SCSI_DCH_U320_MODE */
/*********************************************************************
*
*  SCSIhStandardU320UpdateAbortBitHostMem
*
*     Update 'aborted' bit of the SCB's scontrol field in host memory
*     for standard U320 mode
*
*  Return Value:  none.
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:                
*                  
*********************************************************************/
#if (SCSI_STANDARD_U320_MODE && !(SCSI_ASPI_SUPPORT_GROUP1))
void SCSIhStandardU320UpdateAbortBitHostMem (SCSI_HHCB SCSI_HPTR hhcb,
                                             SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_SCB_STANDARD_U320 SCSI_HPTR scbBuffer =
        (SCSI_SCB_STANDARD_U320 SCSI_HPTR)
         hiob->scbDescriptor->scbBuffer.virtualAddress;
   SCSI_UEXACT8 byteBuf;

   /* Prepare scb to be aborted via 'aborted' bit of scontrol */
   SCSI_GET_LITTLE_ENDIAN8(hhcb, &byteBuf, scbBuffer,
        OSDoffsetof(SCSI_SCB_STANDARD_U320, scontrol));
   SCSI_PUT_LITTLE_ENDIAN8(hhcb, scbBuffer,
        OSDoffsetof(SCSI_SCB_STANDARD_U320, scontrol),
        byteBuf | SCSI_SU320_ABORTED);

   /* Flush the cache */
   SCSI_FLUSH_CACHE(scbBuffer, sizeof(SCSI_SCB_STANDARD_U320));
}
#endif /* (SCSI_STANDARD_U320_MODE && !(SCSI_ASPI_SUPPORT_GROUP1)) */

/*********************************************************************
*
*  SCSIhStandardEnhU320UpdateAbortBitHostMem
*
*     Update 'aborted' bit of the SCB's scontrol field in host memory
*     for standard enhanced U320 mode
*
*  Return Value:  none.
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:                
*                  
*********************************************************************/
#if (SCSI_STANDARD_ENHANCED_U320_MODE && !(SCSI_ASPI_SUPPORT_GROUP1))
void SCSIhStandardEnhU320UpdateAbortBitHostMem (SCSI_HHCB SCSI_HPTR hhcb,
                                                SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_SCB_STANDARD_ENH_U320 SCSI_HPTR scbBuffer =
        (SCSI_SCB_STANDARD_ENH_U320 SCSI_HPTR)
         hiob->scbDescriptor->scbBuffer.virtualAddress;
   SCSI_UEXACT8 byteBuf;

   /* Prepare scb to be aborted via 'aborted' bit of scontrol */
   SCSI_GET_LITTLE_ENDIAN8(hhcb, &byteBuf, scbBuffer,
        OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320, scontrol));
   SCSI_PUT_LITTLE_ENDIAN8(hhcb, scbBuffer,
        OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320, scontrol),
        byteBuf | SCSI_SEU320_ABORTED);

   /* Flush the cache */
   SCSI_FLUSH_CACHE(scbBuffer, sizeof(SCSI_SCB_STANDARD_ENH_U320));
}
#endif /* (SCSI_STANDARD_ENHANCED_U320_MODE && !(SCSI_ASPI_SUPPORT_GROUP1)) */

/*********************************************************************
*
*  SCSIhDchU320UpdateAbortBitHostMem routine -
*
*     Update 'aborted' bit of the SCB's scontrol field in host memory
*     for standard U320 mode
*
*  Return Value:  none.
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:                
*                  
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320UpdateAbortBitHostMem (SCSI_HHCB SCSI_HPTR hhcb,
                                             SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_SCB_DCH_U320 SCSI_HPTR scbBuffer =
        (SCSI_SCB_DCH_U320 SCSI_HPTR)
         hiob->scbDescriptor->scbBuffer.virtualAddress;
   SCSI_UEXACT8 byteBuf;

   /* Prepare scb to be aborted via 'aborted' bit of scontrol */
   SCSI_GET_LITTLE_ENDIAN8(hhcb, &byteBuf, scbBuffer,
        OSDoffsetof(SCSI_SCB_DCH_U320, scontrol));
   SCSI_PUT_LITTLE_ENDIAN8(hhcb, scbBuffer,
        OSDoffsetof(SCSI_SCB_DCH_U320, scontrol),
        byteBuf | SCSI_DCHU320_ABORTED);

   /* Flush the cache */
   SCSI_FLUSH_CACHE(scbBuffer, sizeof(SCSI_SCB_DCH_U320));
}
#endif /* SCSI_DCH_U320_MODE */
/*********************************************************************
*
*  SCSIhStandardU320SetupTR
*
*     Setup Target Reset or Logical Unit request for standard U320 mode
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Setup Target Reset SCB in host memory.
*                 Deliver it to the sequencer.
*
*********************************************************************/
#if (SCSI_STANDARD_U320_MODE && !SCSI_ASPI_SUPPORT_GROUP1)
void SCSIhStandardU320SetupTR (SCSI_HHCB SCSI_HPTR hhcb,
                               SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_SCB_STANDARD_U320 SCSI_IPTR scbBuffer;
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);
#if SCSI_SIMULATION_SUPPORT
   SCSI_BUS_ADDRESS busAddress;
#endif /* SCSI_SIMULATION_SUPPORT */
   SCSI_UEXACT8 zeros[6] = {0,0,0,0,0,0};

   /* Put Target/LUN Reset HIOB into active pointer array */
   SCSI_ACTPTR[hiob->scbNumber] = hiob;

   /* Assign scb buffer descriptor when necessary */
   SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob);

   /* Deliver Target/LUN Reset SCB to sequencer. */
   /* We change to deliver TR SCB to sequencer instead set up break point */
   /* address and put TR SCB at the head of target's execution queue.     */
   /* The purpose is to cover the condition where the sequencer might DMA */
   /* in Target Reset SCB before the break point interrupt occurs because */
   /* a new SCB can be delivered after Target Reset SCB.                  */

   /* Build Target/LUN Reset SCB in the host memory */
   scbBuffer = (SCSI_SCB_STANDARD_U320 SCSI_IPTR) hiob->scbDescriptor->scbBuffer.virtualAddress;

   /* Setup special function - Bit 3 of the SCB control flag */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_U320,scontrol),
                           (SCSI_UEXACT8) SCSI_SU320_SPECFUN);

   /* Clear SCB control1 flag */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_U320,scontrol1),
                           (SCSI_UEXACT8) 0);

   /* Setup the task_attribute and task_management field */
   /* in case this is a packetized request.              */
   /* Setup task_attribute - simple task */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_U320,task_attribute),
                           (SCSI_UEXACT8) SCSI_SIMPLE_TASK_ATTRIBUTE);
 
   if (hiob->cmd==SCSI_CMD_LOGICAL_UNIT_RESET)
   {
      /* Setup task_management - Logical Unit Reset */
      SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_U320,task_management),
                           (SCSI_UEXACT8) SCSI_LUN_RESET);
   }                        
   else
   {
	  /* Setup task_management - Target Reset */
	  SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_U320,task_management),
                           (SCSI_UEXACT8) SCSI_TARGET_RESET);
   }

   /* Setup the special_opcode and special_info field */
   /* in case this is a non-packetized request.       */
   /* Setup special_opcode - message to target */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_special_opcode),
                           (SCSI_UEXACT8) SCSI_SU320_MSGTOTARG);

   if (hiob->cmd==SCSI_CMD_LOGICAL_UNIT_RESET)
   {
      /* Setup special_info - LOGICAL UNIT Reset message */
      SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_special_info),
                           (SCSI_UEXACT8) SCSI_MSGLUR);
   }
   else                        
   {
      /* Setup special_info - Target Reset message */
      SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_special_info),
                           (SCSI_UEXACT8) SCSI_MSG0C);
   }
   
   /* Setup target/LUN ID */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_U320,starget),
                           targetUnit->scsiID);
#if SCSI_PACKETIZED_IO_SUPPORT
   /* Other LUN bytes must be zeroed for packetized requests. */
   SCSI_PUT_BYTE_STRING(hhcb,
                        scbBuffer,
                        OSDoffsetof(SCSI_SCB_STANDARD_U320,slun0),
                        zeros,
                        (SCSI_UEXACT8)SCSI_SU320_SLUN_LENGTH);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_U320,slun),
                           targetUnit->lunID);

   /* Setup cdb length to zero */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_U320,scdb_length),
                           (SCSI_UEXACT8) 0);

   /* Setup the SCB array site of Target Reset SCB */
   SCSI_PUT_LITTLE_ENDIAN16(hhcb,
                            scbBuffer,
                            OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_array_site),
                            hiob->scbDescriptor->scbNumber);

   /* Setup the SCB array site of the next SCB */
#if SCSI_SIMULATION_SUPPORT
   /* Under simulation, the compiler flags error on SCSI_hPUTBUSADDRESS */
   /* macro expands too many levels when CHIM compiling for 64-bit      */
   /* addressing support.  To resolve the problem, CHIM put the next    */
   /* SCB address into a local variable 'busAddress' and pass it in the */
   /* call to SCSI_hPUTBUSADDRESS macro.                                */
   busAddress = hiob->scbDescriptor->queueNext->scbBuffer.busAddress;
   SCSI_hPUTBUSADDRESS(hhcb,
                       scbBuffer,
                       OSDoffsetof(SCSI_SCB_STANDARD_U320,
                                   SCSI_SU320_next_SCB_addr0),
                       busAddress);
#else /* !SCSI_SIMULATION_SUPPORT */
   SCSI_hPUTBUSADDRESS(hhcb,
                       scbBuffer,
                       OSDoffsetof(SCSI_SCB_STANDARD_U320,
                                   SCSI_SU320_next_SCB_addr0),
                       hiob->scbDescriptor->queueNext->scbBuffer.busAddress);
#endif /* SCSI_SIMULATION_SUPPORT */

#if SCSI_RAID1
   /* NULL the mirror_SCB field, to prevent the cmd mirroring by sequencer */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_U320,mirror_SCB)+1,
                           (SCSI_UEXACT8) SCSI_NULL_ENTRY);
#endif /* SCSI_RAID1 */

   /* flush the cache ready for dma */
   SCSI_FLUSH_CACHE(scbBuffer,SCSI_SU320_SIZE_SCB);

   /* Set Target Reset HIOB state needs to be enqueued */
   hiob->SCSI_IP_mgrStat = SCSI_MGR_ENQUE_TR;

   /* bump host new SCB queue offset */
   hhcb->SCSI_HP_hnscbQoff++;

   OSD_OUTEXACT16(SCSI_AICREG(SCSI_HNSCB_QOFF),hhcb->SCSI_HP_hnscbQoff);
}
#endif /* (SCSI_STANDARD_U320_MODE && !SCSI_ASPI_SUPPORT_GROUP1) */

/*********************************************************************
*
*  SCSIhStandardEnhU320SetupTR
*
*     Setup Target/LUN Reset request for standard enhanced U320 mode
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Setup Target Reset SCB in host memory.
*                 Deliver it to the sequencer.
*
*********************************************************************/
#if (SCSI_STANDARD_ENHANCED_U320_MODE && !SCSI_ASPI_SUPPORT_GROUP1)
void SCSIhStandardEnhU320SetupTR (SCSI_HHCB SCSI_HPTR hhcb,
                                  SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_SCB_STANDARD_ENH_U320 SCSI_IPTR scbBuffer;
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);
#if SCSI_SIMULATION_SUPPORT
   SCSI_BUS_ADDRESS busAddress;
#endif /* SCSI_SIMULATION_SUPPORT */

   /* Put Target/LUN Reset HIOB into active pointer array */
   SCSI_ACTPTR[hiob->scbNumber] = hiob;

   /* Assign scb buffer descriptor when necessary */
   SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob);

   /* Deliver Target/LUN Reset SCB to sequencer. */
   /* We change to deliver TR SCB to sequencer instead set up break point */
   /* address and put TR SCB at the head of target's execution queue.     */
   /* The purpose is to cover the condition where the sequencer might DMA */
   /* in Target Reset SCB before the break point interrupt occurs because */
   /* a new SCB can be delivered after Target Reset SCB.                  */

   /* Build Target Reset SCB in the host memory */
   scbBuffer = (SCSI_SCB_STANDARD_ENH_U320 SCSI_IPTR) hiob->scbDescriptor->scbBuffer.virtualAddress;

   /* Setup special function - Bit 3 of the SCB control flag */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,scontrol),
                           (SCSI_UEXACT8) SCSI_SEU320_SPECFUN);

   /* Clear SCB control1 flag */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,scontrol1),
                           (SCSI_UEXACT8) 0);

   /* Setup the task_attribute and task_management field */
   /* in case this is a packetized request.              */
   /* Setup task_attribute - simple task */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,task_attribute),
                           (SCSI_UEXACT8) SCSI_SIMPLE_TASK_ATTRIBUTE);
 
   if (hiob->cmd==SCSI_CMD_LOGICAL_UNIT_RESET)
   {
      /* Setup task_management - Logical Unit Reset */
      SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,task_management),
                           (SCSI_UEXACT8) SCSI_LUN_RESET);
   }                        
   else
   {
      /* Setup task_management - Target Reset */
      SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,task_management),
                           (SCSI_UEXACT8) SCSI_TARGET_RESET);
   }
   
   /* Setup the special_opcode and special_info field */
   /* in case this is a non-packetized request.       */
   /* Setup special_opcode - message to target */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_special_opcode),
                           (SCSI_UEXACT8) SCSI_SEU320_MSGTOTARG);

   if (hiob->cmd==SCSI_CMD_LOGICAL_UNIT_RESET)
   {
      /* Setup special_info - LOGICAL UNIT Reset message */
      SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_special_info),
                           (SCSI_UEXACT8) SCSI_MSGLUR);
   }
   else                        
   {
      /* Setup special_info - Target Reset message */
      SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_special_info),
                           (SCSI_UEXACT8) SCSI_MSG0C);
   }
   /* Setup target/LUN ID */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,starget),
                           targetUnit->scsiID);
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,slun),
                           targetUnit->lunID);

   /* Setup cdb length to zero */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,scdb_length),
                           (SCSI_UEXACT8) 0);

   /* Setup the SCB array site of Target Reset SCB */
   SCSI_PUT_LITTLE_ENDIAN16(hhcb,
                            scbBuffer,
                            OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_array_site),
                            hiob->scbDescriptor->scbNumber);

   /* Setup the SCB array site of the next SCB */
#if SCSI_SIMULATION_SUPPORT
   /* Under simulation, the compiler flags error on SCSI_hPUTBUSADDRESS */
   /* macro expands too many levels when CHIM compiling for 64-bit      */
   /* addressing support.  To resolve the problem, CHIM put the next    */
   /* SCB address into a local variable 'busAddress' and pass it in the */
   /* call to SCSI_hPUTBUSADDRESS macro.                                */
   busAddress = hiob->scbDescriptor->queueNext->scbBuffer.busAddress;
   SCSI_hPUTBUSADDRESS(hhcb,
                       scbBuffer,
                       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                   SCSI_SEU320_next_SCB_addr0),
                       busAddress);
#else /* !SCSI_SIMULATION_SUPPORT */
   SCSI_hPUTBUSADDRESS(hhcb,
                       scbBuffer,
                       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                   SCSI_SEU320_next_SCB_addr0),
                       hiob->scbDescriptor->queueNext->scbBuffer.busAddress);
#endif /* SCSI_SIMULATION_SUPPORT */

#if SCSI_RAID1
   /* NULL the mirror_SCB field, to prevent the cmd mirroring by sequencer */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,mirror_SCB)+1,
                           (SCSI_UEXACT8) SCSI_NULL_ENTRY);
#endif /* SCSI_RAID1 */

   /* flush the cache ready for dma */
   SCSI_FLUSH_CACHE(scbBuffer,SCSI_SU320_SIZE_SCB);

   /* Set Target Reset HIOB state needs to be enqueued */
   hiob->SCSI_IP_mgrStat = SCSI_MGR_ENQUE_TR;

   /* bump host new SCB queue offset */
   hhcb->SCSI_HP_hnscbQoff++;

   OSD_OUTEXACT16(SCSI_AICREG(SCSI_HNSCB_QOFF),hhcb->SCSI_HP_hnscbQoff);
}
#endif /* (SCSI_STANDARD_ENHANCED_U320_MODE && !SCSI_ASPI_SUPPORT_GROUP1) */

/*********************************************************************
*
*  SCSIhDchU320SetupTR
*
*     Setup Target/LUN Reset request for DCH U320 mode
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       Setup Target/LUN Reset SCB in host memory.
*                 Deliver it to the sequencer.
*
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320SetupTR (SCSI_HHCB SCSI_HPTR hhcb,
                          SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_SCB_DCH_U320 SCSI_IPTR scbBuffer;
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);
#if SCSI_SIMULATION_SUPPORT
   SCSI_BUS_ADDRESS busAddress;
#endif /* SCSI_SIMULATION_SUPPORT */

   /* Put Target Reset HIOB into active pointer array */
   SCSI_ACTPTR[hiob->scbNumber] = hiob;

   /* Assign scb buffer descriptor when necessary */
   SCSI_hASSIGNSCBDESCRIPTOR(hhcb,hiob);

   /* Deliver Target/LUN Reset SCB to sequencer. */
   /* We change to deliver TR SCB to sequencer instead set up break point */
   /* address and put TR SCB at the head of target's execution queue.     */
   /* The purpose is to cover the condition where the sequencer might DMA */
   /* in Target Reset SCB before the break point interrupt occurs because */
   /* a new SCB can be delivered after Target Reset SCB.                  */

   /* Build Target/LUN Reset SCB in the host memory */
   scbBuffer = (SCSI_SCB_DCH_U320 SCSI_IPTR) hiob->scbDescriptor->scbBuffer.virtualAddress;

   /* Setup special function - Bit 3 of the SCB control flag */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_DCH_U320,scontrol),
                           (SCSI_UEXACT8) SCSI_DCHU320_SPECFUN);

   /* Clear SCB control1 flag */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_DCH_U320,scontrol1),
                           (SCSI_UEXACT8) 0);

   /* Setup the task_attribute and task_management field */
   /* in case this is a packetized request.              */
   /* Setup task_attribute - simple task */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_DCH_U320,task_attribute),
                           (SCSI_UEXACT8) SCSI_SIMPLE_TASK_ATTRIBUTE);
 
   if (hiob->cmd==SCSI_CMD_LOGICAL_UNIT_RESET)
   {
      /* Setup task_management - Logical Unit Reset */
      SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_DCH_U320,task_management),
                           (SCSI_UEXACT8) SCSI_LUN_RESET);
   }                        
   else
   {
      /* Setup task_management - Target Reset */
      SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_DCH_U320,task_management),
                           (SCSI_UEXACT8) SCSI_TARGET_RESET);
   }
   
   /* Setup the special_opcode and special_info field */
   /* in case this is a non-packetized request.       */
   /* Setup special_opcode - message to target */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_special_opcode),
                           (SCSI_UEXACT8) SCSI_DCHU320_MSGTOTARG);

   if (hiob->cmd==SCSI_CMD_LOGICAL_UNIT_RESET)
   {
      /* Setup special_info - LOGICAL UNIT Reset message */
      SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_special_info),
                           (SCSI_UEXACT8) SCSI_MSGLUR);
   }
   else                        
   {
      /* Setup special_info - Target Reset message */
      SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_special_info),
                           (SCSI_UEXACT8) SCSI_MSG0C);
   }
   
   /* Setup target/LUN ID */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_DCH_U320,starget),
                           targetUnit->scsiID);
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_DCH_U320,slun),
                           targetUnit->lunID);

   /* Setup cdb length to zero */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_DCH_U320,scdb_length),
                           (SCSI_UEXACT8) 0);

   /* Setup the SCB array site of Target Reset SCB */
   SCSI_PUT_LITTLE_ENDIAN16(hhcb,
                            scbBuffer,
                            OSDoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_array_site),
                            hiob->scbDescriptor->scbNumber);

   /* Setup the SCB array site of the next SCB */
#if SCSI_SIMULATION_SUPPORT
   /* Under simulation, the compiler flags error on SCSI_hPUTBUSADDRESS */
   /* macro expands too many levels when CHIM compiling for 64-bit      */
   /* addressing support.  To resolve the problem, CHIM put the next    */
   /* SCB address into a local variable 'busAddress' and pass it in the */
   /* call to SCSI_hPUTBUSADDRESS macro.                                */
   busAddress = hiob->scbDescriptor->queueNext->scbBuffer.busAddress;
   SCSI_hPUTBUSADDRESS(hhcb,
                       scbBuffer,
                       OSDoffsetof(SCSI_SCB_DCH_U320,
                                   SCSI_DCHU320_next_SCB_addr0),
                       busAddress);
#else /* !SCSI_SIMULATION_SUPPORT */
   SCSI_hPUTBUSADDRESS(hhcb,
                       scbBuffer,
                       OSDoffsetof(SCSI_SCB_DCH_U320,
                                   SCSI_DCHU320_next_SCB_addr0),
                       hiob->scbDescriptor->queueNext->scbBuffer.busAddress);
#endif /* SCSI_SIMULATION_SUPPORT */

#if SCSI_RAID1
   /* NULL the mirror_SCB field, to prevent the cmd mirroring by sequencer */
   SCSI_PUT_LITTLE_ENDIAN8(hhcb,
                           scbBuffer,
                           OSDoffsetof(SCSI_SCB_DCH_U320,mirror_SCB)+1,
                           (SCSI_UEXACT8) SCSI_NULL_ENTRY);
#endif /* SCSI_RAID1 */

   /* flush the cache ready for dma */
   SCSI_FLUSH_CACHE(scbBuffer,SCSI_DCHU320_SIZE_SCB);

   /* Set Target Reset HIOB state needs to be enqueued */
   hiob->SCSI_IP_mgrStat = SCSI_MGR_ENQUE_TR;

   /* bump host new SCB queue offset */
   hhcb->SCSI_HP_hnscbQoff++;

   OSD_OUTEXACT16(SCSI_AICREG(SCSI_HNSCB_QOFF),hhcb->SCSI_HP_hnscbQoff);
}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardFreezeHWQueue
*
*     Freeze the hardware queue for the given target id (which is in
*     the given hiob), by removing all the SCBs for this target from
*     the relevant hardware queues.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 Failed hiob
*
*  Remarks:       !!! The sequencer is assumed to be paused and also !!!
*                 !!! assumed to be sitting in the error path        !!!
*
*                 This routine pulls out all the SCBs in the hardware
*                 layer and sends them back to the RSM layer with a
*                 special status (SCSI_SCB_FROZEN) so that the RSM
*                 layer can queue them back to its device queue.
*
*********************************************************************/
#if (SCSI_STANDARD_MODE && (!SCSI_DCH_U320_MODE))
void SCSIhStandardFreezeHWQueue (SCSI_HHCB SCSI_HPTR hhcb,
                                 SCSI_HIOB SCSI_IPTR hiob,
                                 SCSI_UINT16 event)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UNIT_CONTROL SCSI_IPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_UEXACT16 scbCurr;
   SCSI_UEXACT16 scbPrev;
   SCSI_UEXACT16 scbNext;
   SCSI_UEXACT16 scbNumber = 0;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT16 snscbQoff;
   SCSI_HIOB SCSI_IPTR hiobCurr = SCSI_NULL_HIOB;
   SCSI_HIOB SCSI_IPTR hiobPrev;
   SCSI_HIOB SCSI_IPTR hiobNew;
   SCSI_BUS_ADDRESS scbNextbusAddress;
   SCSI_SCB_DESCRIPTOR SCSI_HPTR scbDescriptor;
   SCSI_UEXACT8 scsiId;
   SCSI_UEXACT8 scsiseq;
   SCSI_UEXACT8 modeSave;
   SCSI_UEXACT16 qExeHead,qExeTail;
   SCSI_UEXACT16 qExeTargNextScbArray;
   SCSI_UEXACT16 qNextScbArray;

   /* Save the current mode for now. */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Generate asynchronous event to the RSM layer to notify that the */
   /* hardware layer is going to start freezing of its queues for the */
   /* failed hiob.                                                    */
   SCSI_ASYNC_EVENT_COMMAND(hhcb,hiob,event);

   /* Get the target id for which we are going to pull out SCBs from the */
   /* hardware. */
   scsiId = (SCSI_UEXACT8) targetUnit->scsiID;

   /* Point the nego table index to our scsiId.  We need to look at nego */
   /* data below. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGOADDR), scsiId);

   /* Save SCBPTR */
   scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
         ((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1)) << 8);

   /* Assign the register mapped SCB field (SCBARRAY) variables. */  
   qExeTargNextScbArray =
      (SCSI_UEXACT16)(SCSI_SCB_BASE + SCSI_hQEXETARGNEXTOFFSET(hhcb));
   qNextScbArray =
      (SCSI_UEXACT16)(SCSI_SCB_BASE + SCSI_hQNEXTOFFSET(hhcb));

   /* Is a selection process enabled for our target? */
   if (((scsiseq = (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)))) & SCSI_ENSELO)
       && (OSD_INEXACT8(SCSI_AICREG(SCSI_SELOID)) == scsiId))
   {
      /* Yes, the selection is for our target.  We can remove all the SCBs*/
      /* in the target-specific execution queue except in the following   */
      /* situations:                                                      */
      /*    1. We are freezing for a selection time-out exception (here   */
      /*       the SELTIMO must have been set for the SCB that resulted in*/
      /*       selection time-out.  This SCB is taken care by the         */
      /*       selection time-out handler.  Therefore we cannot post it   */
      /*       back from here.)                                           */
      /*    2. We are freezeing for a packetized target (in this case we  */
      /*       have no control over stopping the actual selection process */
      /*       from happening on the SCSI bus.  Therefore we cannot post  */
      /*       back the SCB from here.)                                   */

      /* Are we in situation #1 (mentioned above)? */
      if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1))) & SCSI_SELTIMO)
      {
         /* Yes we are in situation #1.  Simply point to the next SCB in */
         /* the target-specific execution queue, so that the logic will  */
         /* post back all the SCBs except the one that had the selection */
         /* time-out. */

         scbCurr =
            ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
            (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));

         /* Update the tail of the target-specific execution queue to */
         /* point to the SCB at the head of the queue. */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(
            SCSI_SU320_Q_EXETARG_TAIL+2*scsiId), (SCSI_UEXACT8)scbCurr);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(
            SCSI_SU320_Q_EXETARG_TAIL+2*scsiId+1),
            (SCSI_UEXACT8)(scbCurr >> 8));

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbCurr);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                       (SCSI_UEXACT8)(scbCurr>>8));

         /* Setup the scbCurr to point to the next SCB in the queue, so */
         /* that this SCB and all the ones linked behind this can be    */
         /* removed from the target-specific execution queue. */
         scbCurr =
            (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray)) |
            ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(
                                                  qExeTargNextScbArray+1))) << 8);

         /* Also need to clear the q_exetarg_next entry for the one we skip */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray),
                            (SCSI_UEXACT8)0x0);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray+1),
                            (SCSI_UEXACT8)(SCSI_NULL_SCB));
      }
#if SCSI_PACKETIZED_IO_SUPPORT
      /* Are we in situation #2 (mentioned above)? */
      else if (OSD_INEXACT8(SCSI_AICREG(SCSI_NEGODATA2)) & SCSI_PACKETIZED)
      {
         /* Yes we are in situation #2.  Nullify the scbCurr so that  */
         /* no SCBs will be posted back in the logic below. */
         SCSI_SET_NULL_SCB(scbCurr);
      }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
      else
      {
         scbCurr =
            ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
            (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));

         hiobCurr = SCSI_ACTPTR[scbCurr];

         /* Disable any outstanding selection */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ0),
            scsiseq & ~(SCSI_TEMODEO+SCSI_ENSELO));

         /* Clear target busy entry */ 
         SCSI_hTARGETCLEARBUSY(hhcb,hiobCurr);

         /* To flush the cache */
         scsiseq = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0));

         /* Clear the entry in q_exetarg_tail table to indicate that the  */
         /* target-specific execution queue has been emptied. */
         OSD_OUTEXACT8_HIGH(
            SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*scsiId+1),
            (SCSI_UEXACT8)(SCSI_NULL_SCB));
      }
   }
   else
   {
      /* No, a selection process is not yet enabled, or is not for our       */
      /* target.  The sequencer could be in the middle of preparing for a new*/
      /* selection process for our target.  If so, we can remove all the SCBs*/
      /* except the first one in the target-specific execution queue. If not,*/
      /* we can remove all the SCBs. */
      
      /* To find out whether the sequencer is in the middle of preparing for */
      /* the selection for our target, we need to walk the execution queue.  */
      /* Since we are going to walk the execution queue in the logic below,  */
      /* we can figure out this situation in that logic. */

      scbPrev = scbCurr = qExeHead =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD)) |
         ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(
         SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1))) << 8);
      qExeTail =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL)) |
         ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(
         SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL+1))) << 8);

      while (!SCSI_IS_NULL_SCB(scbCurr))
      {
         hiobCurr = SCSI_ACTPTR[scbCurr];

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbCurr);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbCurr>>8));

         /* Search for matching target id */
         if ((SCSI_UEXACT8)(SCSI_TARGET_UNIT(hiobCurr)->scsiID) == scsiId)
         {
            /* May the sequencer be in the middle of preparing a selection   */
            /* process for our target?  The following conditions might point */
            /* towards that:                                                 */
            /*    a) Mode 3 SCBPTR has been updated by the sequencer to point*/
            /*       to our SCB at the head of target-specific queue of our  */
            /*       target, or                                              */
            /*    b) Mode 3 SCBPTR is pointing to the previous SCB, in which */
            /*       case the sequencer may be about to set the SCBPTR to our*/
            /*       SCB, or                                                 */
            /*    c) There is only one element in the common execution queue,*/
            /*       in which case the sequencer could have just checked that*/
            /*       there is an SCB for which a selection can be started,   */
            /*       but has not yet updated the Mode 3 SCBPTR.              */
            /* The sequencer will not be in the middle of preparation for a  */
            /* selection in the following case:                              */
            /*    1. If the exception happens due to a switch from           */
            /*       packetized to non-packetized.  In this case the SCB has */
            /*       been put back at the head of the execution queue in the */
            /*       exception handler.  The sequencer will know that we have*/
            /*       put an SCB in the execution only after we unpause it.   */
            /*       Therefore the sequencer will not be in the middle of    */
            /*       preparing a selection for this SCB. This case can be    */
            /*       identified by the fact that we reach here due to the    */
            /*       SCSI_AE_PACKTONONPACK_DETECTED event. */
#if SCSI_PACKETIZED_IO_SUPPORT
            if (((scbCurr == scbptr) ||
                 ((scbPrev == scbptr) && (scbCurr != qExeHead)) ||
                 (qExeHead == qExeTail)) &&
                (event != SCSI_AE_PACKTONONPACK_DETECTED))
#else
            if ((scbCurr == scbptr) ||
                ((scbPrev == scbptr) && (scbCurr != qExeHead)) ||
                (qExeHead == qExeTail))
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
            {

               /* Yes, the sequencer may be in the middle of the preparation.*/
               /* We can remove all the SCBs except the one in the head of   */
               /* the target-specific execution queue. */

               /* Update the tail of the target-specific execution queue to  */
               /* point to the SCB at the head of the queue. */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(
                  SCSI_SU320_Q_EXETARG_TAIL+2*scsiId), (SCSI_UEXACT8)scbCurr);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(
                  SCSI_SU320_Q_EXETARG_TAIL+2*scsiId+1),
                  (SCSI_UEXACT8)(scbCurr >> 8));

               /* Setup the scbCurr to point to the next SCB in the queue, so*/
               /* that this SCB and all the ones linked behind this can be   */
               /* removed from the target-specific execution queue. */
               scbCurr =
                  (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray)) |
                  ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(
                                                        qExeTargNextScbArray+1))) << 8);

               /* Also need to clear the q_exetarg_next entry for the one we skip */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray),
                                  (SCSI_UEXACT8)0x0);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray+1),
                                  (SCSI_UEXACT8)(SCSI_NULL_SCB));
            }
            else
            {
               /* No, the sequencer is not in the middle of the preparation. */
               /* We can remove all the SCBs from the target-specific exe    */
               /* queue. */

               /* Remove the SCB from execution queue */
               scbNext =
                  (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qNextScbArray)) |
                   ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(
                                                         qNextScbArray+1))) << 8);
                        
               /* Mending the execution queue */
               if (scbCurr == scbPrev)
               {
                  scbPrev = scbNext;

                  /* Remove the SCB at the queue head */
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD),
                     (SCSI_UEXACT8)scbNext);
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1),
                     (SCSI_UEXACT8)(scbNext>>8));
               }
               else
               {
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                                (SCSI_UEXACT8)scbPrev);
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                                (SCSI_UEXACT8)(scbPrev>>8));
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray),
                                     (SCSI_UEXACT8)scbNext);
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(qNextScbArray+1),
                                     (SCSI_UEXACT8)(scbNext >> 8));

                  if (scbCurr == ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(
                                   SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL)) |
                                   ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(
                                   SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL+1)))<<8)))
                  {
                     /* Remove the SCB at the queue tail */
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL),
                                        (SCSI_UEXACT8)scbPrev);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_TAIL+1),
                                        (SCSI_UEXACT8)(scbPrev>>8));
                  }
               }

               /* Clear the entry in q_exetarg_tail table to indicate that */
               /* the target-specific execution queue has been emptied. */
               OSD_OUTEXACT8_HIGH(
                  SCSI_AICREG(SCSI_SU320_Q_EXETARG_TAIL+2*scsiId+1),
                  (SCSI_UEXACT8)(SCSI_NULL_SCB));
            }

            break;
         }
         else
         {
            /* Keep previous SCB */
            scbPrev = scbCurr;
      
            /* Advance to next SCB in Execution queue */
            scbCurr =
               (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qNextScbArray)) |
               ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(
                                                     qNextScbArray+1)))<<8);
         }
      }
   }

   /* Remove all the SCBs in the target-specific execution queue of our    */
   /* target, starting from scbCurr setup by the logic above. Post these   */
   /* SCBs back to the upper layer so that it can queue them in its queue. */
   while (!SCSI_IS_NULL_SCB(scbCurr))
   {
      hiobCurr = SCSI_ACTPTR[scbCurr];

      /* free the associated entry in active pointer array */
      SCSI_ACTPTR[hiobCurr->scbNumber] = SCSI_NULL_HIOB;

      /* Put proper status in HIOB, so that the upper layer can queue  */
      /* it back into the device queue. */
      hiobCurr->stat = SCSI_SCB_FROZEN;

      /* Free the SCB descriptor - if required. */
      SCSI_hFREESCBDESCRIPTOR(hhcb,hiobCurr);

      /* Post back this HIOB */
      if (hiobCurr->cmd == SCSI_CMD_INITIATE_TASK)
      {
         SCSI_COMPLETE_HIOB(hiobCurr);
      }
      else
      {
         SCSI_COMPLETE_SPECIAL_HIOB(hiobCurr);
      }

      /* Make sure to put scbptr pointing to the found scb */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbCurr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbCurr>>8));

      scbCurr =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(qExeTargNextScbArray)) |
         ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(
                                               qExeTargNextScbArray+1))) << 8);
   }

   /* Restore SCBPTR */   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbptr >> 8));

   /* Obtain scbnumber sitting at the head of the new queue and its        */
   /* corresponding hiob.  To get the scbNumber we need to get the         */
   /* q_new_pointer first and then search the active  pointer array to get */
   /* the hiob containing the same address. */
#if SCSI_ASPI_SUPPORT

   SCSI_hGETADDRESSSCRATCH(hhcb,SCSI_SU320_Q_NEW_POINTER);
   scbNextbusAddress = hhcb->busAddress;
#else

   scbNextbusAddress = SCSI_hGETADDRESSSCRATCH(hhcb,SCSI_SU320_Q_NEW_POINTER);

#endif /* SCSI_ASPI_SUPPORT */
   for (scbNumber = 0; scbNumber < hhcb->numberScbs; scbNumber++)
   {
      /* Obtain HIOB in Active pointer array */ 

      hiobCurr = SCSI_ACTPTR[scbNumber];

      if (hiobCurr != SCSI_NULL_HIOB)
      {
         if (!SCSIhCompareBusAddress(hiobCurr->scbDescriptor->scbBuffer.busAddress,
             scbNextbusAddress))
         {
            break;
         }
         else if (scbNumber == (hhcb->numberScbs-1))
         {
            hiobCurr = SCSI_NULL_HIOB;
         }
      }
   }
   hiobNew = hiobCurr;

   /* Switch to mode 2 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);

   /* If an SCB for this target is getting DMAed, just disable the DMA (this */
   /* hiob need not be posted back, as it will be done in the next step. */
   if ((OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL)) &
       (SCSI_CCSCBEN+SCSI_CCSCBDIR+SCSI_CCARREN)) ==
       (SCSI_CCSCBEN+SCSI_CCSCBDIR+SCSI_CCARREN))
   {
      /* Take the SCB from the head of the new queue and check if it is for  */
      /* the failed target.                                                  */
      if ((SCSI_UEXACT8)(SCSI_TARGET_UNIT(hiobCurr)->scsiID) == scsiId)
      {
         /* Disable DMA */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSCBCTL),0);

         /* Since the sequencer has already incremented the SNSCB_QOFF, it */
         /* should be decremented as it will be adjusted during posting    */
         /* back in the logic below. */
         snscbQoff = SCSIhGetArpNewSCBQOffset(hhcb) - (SCSI_UEXACT16)1;
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF),
               (SCSI_UEXACT8)snscbQoff);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF+1),
               (SCSI_UEXACT8)(snscbQoff >> 8));
      }
      else
      {
         /* Obtain the next SCB in the new queue and check if it is for the */
         /* failed target.  We have to disable DMA even in this case because*/
         /* after the DMA the q_new_pointer and array_site_next variables   */
         /* will be modified by the sequencer to point to this SCB; but this*/
         /* SCB will be removed while we are searching the new queue down   */
         /* in the logic. */

         /* .....Obtain next Scb info */   
         scbDescriptor=hiobCurr->scbDescriptor->queueNext;

         scbCurr = scbDescriptor->scbNumber;
         if (!SCSI_IS_NULL_SCB(scbCurr))
         {
            hiobCurr = SCSI_ACTPTR[scbCurr];
         }
         else
         {
            hiobCurr = SCSI_NULL_HIOB;
         }

         if ((hiobCurr != SCSI_NULL_HIOB) &&
             ((SCSI_UEXACT8)(SCSI_TARGET_UNIT(hiobCurr)->scsiID) == scsiId))
         {
            /* Disable DMA */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSCBCTL),0);

            /* Since the sequencer has already incremented the SNSCB_QOFF, */
            /* it should be decremented as it will be adjusted during      */
            /* posting back in the logic below. */
            snscbQoff = SCSIhGetArpNewSCBQOffset(hhcb) - (SCSI_UEXACT16)1;
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF),
                  (SCSI_UEXACT8)snscbQoff);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF+1),
                  (SCSI_UEXACT8)(snscbQoff >> 8));
         }
      }
   }

   /* Post back all the SCBs for the failed target from the New Queue.     */
   hiobCurr = hiobNew;
   scbCurr = scbPrev = scbNumber;

   /* Have the search completed? */
   while (hiobCurr != SCSI_NULL_HIOB) 
   {
      if ((SCSI_UEXACT8)(SCSI_TARGET_UNIT(hiobCurr)->scsiID) == scsiId)
      {
         /* Decrement and update host new SCB queue offset register */
         hhcb->SCSI_HP_hnscbQoff--;
         OSD_OUTEXACT16(SCSI_AICREG(SCSI_HNSCB_QOFF),hhcb->SCSI_HP_hnscbQoff);

         /* Remove aborted HIOB from New queue and mend the queue */

         /* .....Obtain next Scb info */   
         scbDescriptor=hiobCurr->scbDescriptor->queueNext;
         scbNext = scbDescriptor->scbNumber;
 
         if (scbCurr == scbNumber)
         {
            scbNumber = scbPrev = scbNext;
            SCSI_hSETADDRESSSCRATCH(hhcb,SCSI_SU320_Q_NEW_POINTER,
               hiobCurr->scbDescriptor->queueNext->scbBuffer.virtualAddress);
         }
         else
         {
            /* Obtain previous HIOB in Active pointer array */ 
            hiobPrev = SCSI_ACTPTR[scbPrev];
            SCSI_hUPDATENEXTSCBADDRESS(hhcb,hiobPrev,
               hiobCurr->scbDescriptor->queueNext->scbBuffer.busAddress);

            /* The scbDescriptor->queueNext also needs to be mended  */ 
            /* since we use it to search elements in new queue now   */
            hiobPrev->scbDescriptor->queueNext = hiobCurr->scbDescriptor->queueNext;
         }

         /* free the associated entry in active pointer array */
         SCSI_ACTPTR[scbCurr] = SCSI_NULL_HIOB;

         /* Put proper status in HIOB, so that rsm layer will queue it back */
         /* into the device queue.                                          */
         hiobCurr->stat = SCSI_SCB_FROZEN;

         /* Free the SCB descriptor - if required. */
         SCSI_hFREESCBDESCRIPTOR(hhcb,hiobCurr);

         /* Post back this HIOB */
         if (hiobCurr->cmd == SCSI_CMD_INITIATE_TASK)
         {
            SCSI_COMPLETE_HIOB(hiobCurr);
         }
         else
         {
            SCSI_COMPLETE_SPECIAL_HIOB(hiobCurr);
         }

         scbCurr = scbNext;
      }
      else
      {
         /* Need to get next scb from current scb */
         scbPrev = scbCurr;
      
         /* .....Obtain next Scb info */   
         scbDescriptor=hiobCurr->scbDescriptor->queueNext;
         scbCurr = scbDescriptor->scbNumber;

      }
      
      /* We reach the end of the new queue if the current scb is NULL. */
      /* So, just exit the search. */
      if (SCSI_IS_NULL_SCB(scbCurr))
      {
         break;
      }
      else
      {
         /* ... obtain HIOB in Active pointer array */
         hiobCurr = SCSI_ACTPTR[scbCurr];
      }
   }

   /* Generate asynchronous event to the RSM layer to notify that the      */
   /* hardware layer has finished freezing its queues for the failed hiob. */
   if (event == SCSI_AE_FREEZEONERROR_START)
   {
      event = SCSI_AE_FREEZEONERROR_END;
      SCSI_ASYNC_EVENT_COMMAND(hhcb,hiob,event);
   }

   /* Restore MODE_PTR */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
}
#endif /* (SCSI_STANDARD_MODE && (!SCSI_DCH_U320_MODE)) */

/*********************************************************************
*
*  SCSIhDchU320FreezeHWQueue
*
*     Freeze the hardware queue for the given target id (which is in
*     the given hiob), by removing all the SCBs for this target from
*     the relevant hardware queues.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 Failed hiob
*
*  Remarks:       !!! The sequencer is assumed to be paused and also !!!
*                 !!! assumed to be sitting in the error path        !!!
*
*                 This routine pulls out all the SCBs in the hardware
*                 layer and sends them back to the RSM layer with a
*                 special status (SCSI_SCB_FROZEN) so that the RSM
*                 layer can queue them back to its device queue.
*
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320FreezeHWQueue (SCSI_HHCB SCSI_HPTR hhcb,
                                SCSI_HIOB SCSI_IPTR hiob, 
                                SCSI_UINT16 event)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UNIT_CONTROL SCSI_IPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_UEXACT16 scbCurr;
   SCSI_UEXACT16 scbPrev;
   SCSI_UEXACT16 scbNext;
   SCSI_UEXACT16 scbNumber = 0;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT16 snscbQoff;
   SCSI_HIOB SCSI_IPTR hiobCurr = SCSI_NULL_HIOB;
   SCSI_HIOB SCSI_IPTR hiobPrev;
   SCSI_HIOB SCSI_IPTR hiobNew;
   SCSI_BUS_ADDRESS scbNextbusAddress;
   SCSI_SCB_DESCRIPTOR SCSI_HPTR scbDescriptor;
   SCSI_UEXACT8 scsiId;
   SCSI_UEXACT8 scsiseq;
   SCSI_UEXACT8 modeSave;
   SCSI_UEXACT16 qExeHead,qExeTail;

   /* Save the current mode for now. */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Generate asynchronous event to the RSM layer to notify that the */
   /* hardware layer is going to start freezing of its queues for the */
   /* failed hiob.                                                    */
   SCSI_ASYNC_EVENT_COMMAND(hhcb,hiob,event);

   /* Get the target id for which we are going to pull out SCBs from the */
   /* hardware. */
   scsiId = (SCSI_UEXACT8) targetUnit->scsiID;

   /* Point the nego table index to our scsiId.  We need to look at nego */
   /* data below. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGOADDR), scsiId);

   /* Save SCBPTR */
   scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
         ((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1)) << 8);

   /* Is a selection process enabled for our target? */
   if (((scsiseq = (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0)))) & SCSI_ENSELO)
       && (OSD_INEXACT8(SCSI_AICREG(SCSI_SELOID)) == scsiId))
   {
      /* Yes, the selection is for our target.  We can remove all the SCBs*/
      /* in the target-specific execution queue except in the following   */
      /* situations:                                                      */
      /*    1. We are freezing for a selection time-out exception (here   */
      /*       the SELTIMO must have been set for the SCB that resulted in*/
      /*       selection time-out.  This SCB is taken care by the         */
      /*       selection time-out handler.  Therefore we cannot post it   */
      /*       back from here.)                                           */
      /*    2. We are freezeing for a packetized target (in this case we  */
      /*       have no control over stopping the actual selection process */
      /*       from happening on the SCSI bus.  Therefore we cannot post  */
      /*       back the SCB from here.)                                   */

      /* Are we in situation #1 (mentioned above)? */
      if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1))) & SCSI_SELTIMO)
      {
         /* Yes we are in situation #1.  Simply point to the next SCB in */
         /* the target-specific execution queue, so that the logic will  */
         /* post back all the SCBs except the one that had the selection */
         /* time-out. */

         scbCurr =
            ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
            (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));

         /* Update the tail of the target-specific execution queue to */
         /* point to the SCB at the head of the queue. */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(
            SCSI_DCHU320_Q_EXETARG_TAIL+2*scsiId), (SCSI_UEXACT8)scbCurr);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(
            SCSI_DCHU320_Q_EXETARG_TAIL+2*scsiId+1),
            (SCSI_UEXACT8)(scbCurr >> 8));

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbCurr);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
               (SCSI_UEXACT8)(scbCurr>>8));

         /* Setup the scbCurr to point to the next SCB in the queue, so */
         /* that this SCB and all the ones linked behind this can be    */
         /* removed from the target-specific execution queue. */
         scbCurr =
            (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next))) |
            ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
            OSDoffsetof(SCSI_SCB_DCH_U320,
            SCSI_DCHU320_q_exetarg_next)+1))) << 8);
            
         /* Also need to clear the q_exetarg_next entry for the one we skip */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)),
             (SCSI_UEXACT8)0x0);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)+1),
             (SCSI_UEXACT8)(SCSI_NULL_SCB));
      }
#if SCSI_PACKETIZED_IO_SUPPORT
      /* Are we in situation #2 (mentioned above)? */
      else if (OSD_INEXACT8(SCSI_AICREG(SCSI_NEGODATA2)) & SCSI_PACKETIZED)
      {
         /* Yes we are in situation #2.  Nullify the scbCurr so that  */
         /* no SCBs will be posted back in the logic below. */
         SCSI_SET_NULL_SCB(scbCurr);
      }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
      else
      {
         scbCurr =
            ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB+1))) << 8) |
            (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_WAITING_SCB)));

         hiobCurr = SCSI_ACTPTR[scbCurr];

         /* Disable any outstanding selection */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ0),
            scsiseq & ~(SCSI_TEMODEO+SCSI_ENSELO));

         /* Clear target busy entry */ 
         SCSI_hTARGETCLEARBUSY(hhcb,hiobCurr);

         /* To flush the cache */
         scsiseq = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISEQ0));

         /* Clear the entry in q_exetarg_tail table to indicate that the  */
         /* target-specific execution queue has been emptied. */
         OSD_OUTEXACT8_HIGH(
            SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*scsiId+1),
            (SCSI_UEXACT8)(SCSI_NULL_SCB));
      }
   }
   else
   {
      /* No, a selection process is not yet enabled, or is not for our       */
      /* target.  The sequencer could be in the middle of preparing for a new*/
      /* selection process for our target.  If so, we can remove all the SCBs*/
      /* except the first one in the target-specific execution queue. If not,*/
      /* we can remove all the SCBs. */
      
      /* To find out whether the sequencer is in the middle of preparing for */
      /* the selection for our target, we need to walk the execution queue.  */
      /* Since we are going to walk the execution queue in the logic below,  */
      /* we can figure out this situation in that logic. */

      scbPrev = scbCurr = qExeHead =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD)) |
         ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(
         SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1))) << 8);
      qExeTail =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL)) |
         ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(
         SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL+1))) << 8);

      while (!SCSI_IS_NULL_SCB(scbCurr))
      {
         hiobCurr = SCSI_ACTPTR[scbCurr];

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbCurr);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbCurr>>8));

         /* Search for matching target id */
         if ((SCSI_UEXACT8)(SCSI_TARGET_UNIT(hiobCurr)->scsiID) == scsiId)
         {
            /* May the sequencer be in the middle of preparing a selection   */
            /* process for our target?  The following conditions might point */
            /* towards that:                                                 */
            /*    a) Mode 3 SCBPTR has been updated by the sequencer to point*/
            /*       to our SCB at the head of target-specific queue of our  */
            /*       target, or                                              */
            /*    b) Mode 3 SCBPTR is pointing to the previous SCB, in which */
            /*       case the sequencer may be about to set the SCBPTR to our*/
            /*       SCB, or                                                 */
            /*    c) There is only one element in the common execution queue,*/
            /*       in which case the sequencer could have just checked that*/
            /*       there is an SCB for which a selection can be started,   */
            /*       but has not yet updated the Mode 3 SCBPTR.              */
            /* The sequencer will not be in the middle of preparation for a  */
            /* selection in the following case:                              */
            /*    1. If the exception happens due to a switch from           */
            /*       packetized to non-packetized.  In this case the SCB has */
            /*       been put back at the head of the execution queue in the */
            /*       exception handler.  The sequencer will know that we have*/
            /*       put an SCB in the execution only after we unpause it.   */
            /*       Therefore the sequencer will not be in the middle of    */
            /*       preparing a selection for this SCB. This case can be    */
            /*       identified by the fact that we reach here due to the    */
            /*       SCSI_AE_PACKTONONPACK_DETECTED event. */
#if SCSI_PACKETIZED_IO_SUPPORT
            if (((scbCurr == scbptr) ||
                 ((scbPrev == scbptr) && (scbCurr != qExeHead)) ||
                 (qExeHead == qExeTail)) &&
                (event != SCSI_AE_PACKTONONPACK_DETECTED))
#else
            if ((scbCurr == scbptr) ||
                ((scbPrev == scbptr) && (scbCurr != qExeHead)) ||
                (qExeHead == qExeTail))
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
            {

               /* Yes, the sequencer may be in the middle of the preparation.*/
               /* We can remove all the SCBs except the one in the head of   */
               /* the target-specific execution queue. */

               /* Update the tail of the target-specific execution queue to  */
               /* point to the SCB at the head of the queue. */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(
                  SCSI_DCHU320_Q_EXETARG_TAIL+2*scsiId), (SCSI_UEXACT8)scbCurr);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(
                  SCSI_DCHU320_Q_EXETARG_TAIL+2*scsiId+1),
                  (SCSI_UEXACT8)(scbCurr >> 8));

               /* Setup the scbCurr to point to the next SCB in the queue, so*/
               /* that this SCB and all the ones linked behind this can be   */
               /* removed from the target-specific execution queue. */
               scbCurr =
                  (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                  OSDoffsetof(SCSI_SCB_DCH_U320,
                  SCSI_DCHU320_q_exetarg_next))) |
                  ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                  OSDoffsetof(SCSI_SCB_DCH_U320,
                  SCSI_DCHU320_q_exetarg_next)+1))) << 8);
                  
               /* Also need to clear the q_exetarg_next entry for the one we skip */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                   OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)),
                   (SCSI_UEXACT8)0x0);
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                   OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next)+1),
                   (SCSI_UEXACT8)(SCSI_NULL_SCB));
            }
            else
            {
               /* No, the sequencer is not in the middle of the preparation. */
               /* We can remove all the SCBs from the target-specific exe    */
               /* queue. */

               /* Remove the SCB from execution queue */
               scbNext = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(
                  SCSI_AICREG(SCSI_SCB_BASE+
                  OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next))) |
                  ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                  OSDoffsetof(SCSI_SCB_DCH_U320,
                  SCSI_DCHU320_q_next)+1))) << 8);
                        
               /* Mending the execution queue */
               if (scbCurr == scbPrev)
               {
                  scbPrev = scbNext;

                  /* Remove the SCB at the queue head */
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD),
                     (SCSI_UEXACT8)scbNext);
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1),
                     (SCSI_UEXACT8)(scbNext>>8));
               }
               else
               {
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                     (SCSI_UEXACT8)scbPrev);
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                     (SCSI_UEXACT8)(scbPrev>>8));
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)),
                     (SCSI_UEXACT8)scbNext);
                  OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1),
                     (SCSI_UEXACT8)(scbNext >> 8));

                  if (scbCurr == ((SCSI_UEXACT16)OSD_INEXACT8_HIGH(
                                   SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL)) |
                                   ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(
                                   SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL+1)))<<8)))
                  {
                     /* Remove the SCB at the queue tail */
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL),
                        (SCSI_UEXACT8)scbPrev);
                     OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_TAIL+1),
                        (SCSI_UEXACT8)(scbPrev>>8));
                  }
               }

               /* Clear the entry in q_exetarg_tail table to indicate that */
               /* the target-specific execution queue has been emptied. */
               OSD_OUTEXACT8_HIGH(
                  SCSI_AICREG(SCSI_DCHU320_Q_EXETARG_TAIL+2*scsiId+1),
                  (SCSI_UEXACT8)(SCSI_NULL_SCB));
            }

            break;
         }
         else
         {
            /* Keep previous SCB */
            scbPrev = scbCurr;
      
            /* Advance to next SCB in Execution queue */
            scbCurr =
               (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
               OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next))) |
               ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
               OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1)))<<8);
         }
      }
   }

   /* Remove all the SCBs in the target-specific execution queue of our    */
   /* target, starting from scbCurr setup by the logic above. Post these   */
   /* SCBs back to the upper layer so that it can queue them in its queue. */
   while (!SCSI_IS_NULL_SCB(scbCurr))
   {
      hiobCurr = SCSI_ACTPTR[scbCurr];

      /* free the associated entry in active pointer array */
      SCSI_ACTPTR[hiobCurr->scbNumber] = SCSI_NULL_HIOB;

      /* Put proper status in HIOB, so that the upper layer can queue  */
      /* it back into the device queue. */
      hiobCurr->stat = SCSI_SCB_FROZEN;

      /* Free the SCB descriptor - if required. */
      SCSI_hFREESCBDESCRIPTOR(hhcb,hiobCurr);

      /* Post back this HIOB */
      if (hiobCurr->cmd == SCSI_CMD_INITIATE_TASK)
      {
         SCSI_COMPLETE_HIOB(hiobCurr);
      }
      else
      {
         SCSI_COMPLETE_SPECIAL_HIOB(hiobCurr);
      }

      /* Make sure to put scbptr pointing to the found scb */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbCurr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbCurr>>8));

      scbCurr =
         (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_exetarg_next))) |
         ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DCH_U320,
         SCSI_DCHU320_q_exetarg_next)+1))) << 8);
   }

   /* Restore SCBPTR */   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbptr >> 8));

   /* Obtain scbnumber sitting at the head of the new queue and its        */
   /* corresponding hiob.  To get the scbNumber we need to get the         */
   /* q_new_pointer first and then search the active  pointer array to get */
   /* the hiob containing the same address. */
   scbNextbusAddress = SCSI_hGETADDRESSSCRATCH(hhcb,SCSI_DCHU320_Q_NEW_POINTER);

   for (scbNumber = 0; scbNumber < hhcb->numberScbs; scbNumber++)
   {
      /* Obtain HIOB in Active pointer array */ 

      hiobCurr = SCSI_ACTPTR[scbNumber];

      if (hiobCurr != SCSI_NULL_HIOB)
      {
         if (!SCSIhCompareBusAddress(hiobCurr->scbDescriptor->scbBuffer.busAddress,
             scbNextbusAddress))
         {
            break;
         }
         else if (scbNumber == (hhcb->numberScbs-1))
         {
            hiobCurr = SCSI_NULL_HIOB;
         }
      }
   }
   hiobNew = hiobCurr;

   /* Switch to mode 2 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);

   /* If an SCB for this target is getting DMAed, just disable the DMA (this */
   /* hiob need not be posted back, as it will be done in the next step. */
   if ((OSD_INEXACT8(SCSI_AICREG(SCSI_CCSCBCTL)) &
       (SCSI_CCSCBEN+SCSI_CCSCBDIR+SCSI_CCARREN)) ==
       (SCSI_CCSCBEN+SCSI_CCSCBDIR+SCSI_CCARREN))
   {
      /* Take the SCB from the head of the new queue and check if it is for  */
      /* the failed target.                                                  */
      if ((SCSI_UEXACT8)(SCSI_TARGET_UNIT(hiobCurr)->scsiID) == scsiId)
      {
         /* Disable DMA */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSCBCTL),0);

         /* Since the sequencer has already incremented the SNSCB_QOFF, it */
         /* should be decremented as it will be adjusted during posting    */
         /* back in the logic below. */
         snscbQoff = SCSIhGetArpNewSCBQOffset(hhcb) - (SCSI_UEXACT16)1;
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF),
               (SCSI_UEXACT8)snscbQoff);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF+1),
               (SCSI_UEXACT8)(snscbQoff >> 8));
      }
      else
      {
         /* Obtain the next SCB in the new queue and check if it is for the */
         /* failed target.  We have to disable DMA even in this case because*/
         /* after the DMA the q_new_pointer and array_site_next variables   */
         /* will be modified by the sequencer to point to this SCB; but this*/
         /* SCB will be removed while we are searching the new queue down   */
         /* in the logic. */

         /* .....Obtain next Scb info */   
         scbDescriptor=hiobCurr->scbDescriptor->queueNext;

         scbCurr = scbDescriptor->scbNumber;
         if (!SCSI_IS_NULL_SCB(scbCurr))
         {
            hiobCurr = SCSI_ACTPTR[scbCurr];
         }
         else
         {
            hiobCurr = SCSI_NULL_HIOB;
         }

         if ((hiobCurr != SCSI_NULL_HIOB) &&
             ((SCSI_UEXACT8)(SCSI_TARGET_UNIT(hiobCurr)->scsiID) == scsiId))
         {
            /* Disable DMA */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSCBCTL),0);

            /* Since the sequencer has already incremented the SNSCB_QOFF, */
            /* it should be decremented as it will be adjusted during      */
            /* posting back in the logic below. */
            snscbQoff = SCSIhGetArpNewSCBQOffset(hhcb) - (SCSI_UEXACT16)1;
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF),
                  (SCSI_UEXACT8)snscbQoff);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF+1),
                  (SCSI_UEXACT8)(snscbQoff >> 8));
         }
      }
   }

   /* Post back all the SCBs for the failed target from the New Queue.     */
   hiobCurr = hiobNew;
   scbCurr = scbPrev = scbNumber;

   /* Have the search completed? */
   while (hiobCurr != SCSI_NULL_HIOB) 
   {
      if ((SCSI_UEXACT8)(SCSI_TARGET_UNIT(hiobCurr)->scsiID) == scsiId)
      {
         /* Decrement and update host new SCB queue offset register */
         hhcb->SCSI_HP_hnscbQoff--;
         OSD_OUTEXACT16(SCSI_AICREG(SCSI_HNSCB_QOFF),hhcb->SCSI_HP_hnscbQoff);

         /* Remove aborted HIOB from New queue and mend the queue */

         /* .....Obtain next Scb info */   
         scbDescriptor=hiobCurr->scbDescriptor->queueNext;
         scbNext = scbDescriptor->scbNumber;
 
         if (scbCurr == scbNumber)
         {
            scbNumber = scbPrev = scbNext;
            SCSI_hSETADDRESSSCRATCH(hhcb,SCSI_DCHU320_Q_NEW_POINTER,
               hiobCurr->scbDescriptor->queueNext->scbBuffer.virtualAddress);
         }
         else
         {
            /* Obtain previous HIOB in Active pointer array */ 
            hiobPrev = SCSI_ACTPTR[scbPrev];
            SCSI_hUPDATENEXTSCBADDRESS(hhcb,hiobPrev,
               hiobCurr->scbDescriptor->queueNext->scbBuffer.busAddress);

            /* The scbDescriptor->queueNext also needs to be mended  */ 
            /* since we use it to search elements in new queue now   */
            hiobPrev->scbDescriptor->queueNext = hiobCurr->scbDescriptor->queueNext;
         }

         /* free the associated entry in active pointer array */
         SCSI_ACTPTR[scbCurr] = SCSI_NULL_HIOB;

         /* Put proper status in HIOB, so that rsm layer will queue it back */
         /* into the device queue.                                          */
         hiobCurr->stat = SCSI_SCB_FROZEN;

         /* Free the SCB descriptor - if required. */
         SCSI_hFREESCBDESCRIPTOR(hhcb,hiobCurr);

         /* Post back this HIOB */
         if (hiobCurr->cmd == SCSI_CMD_INITIATE_TASK)
         {
            SCSI_COMPLETE_HIOB(hiobCurr);
         }
         else
         {
            SCSI_COMPLETE_SPECIAL_HIOB(hiobCurr);
         }

         scbCurr = scbNext;
      }
      else
      {
         /* Need to get next scb from current scb */
         scbPrev = scbCurr;
      
         /* .....Obtain next Scb info */   
         scbDescriptor=hiobCurr->scbDescriptor->queueNext;
         scbCurr = scbDescriptor->scbNumber;

      }
      
      /* We reach the end of the new queue if the current scb is NULL. */
      /* So, just exit the search. */
      if (SCSI_IS_NULL_SCB(scbCurr))
      {
         break;
      }
      else
      {
         /* ... obtain HIOB in Active pointer array */
         hiobCurr = SCSI_ACTPTR[scbCurr];
      }
   }

   /* Generate asynchronous event to the RSM layer to notify that the      */
   /* hardware layer has finished freezing its queues for the failed hiob. */
   if (event == SCSI_AE_FREEZEONERROR_START)
   {
      event = SCSI_AE_FREEZEONERROR_END;
      SCSI_ASYNC_EVENT_COMMAND(hhcb,hiob,event);
   }

   /* Restore MODE_PTR */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
}
#endif /* SCSI_DCH_U320_MODE */
/*********************************************************************
*
*  SCSIhStandardPackNonPackQueueHIOB
*
*     A Pack to Non Pack has been detected when the target may have not
*     been idle. This routine freezes the execution queue, moves SCBs
*     from the execution queue to the device queue, then dispatches the
*     queued HIOBs.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       !!! The sequencer is assumed to be paused and also !!!
*                 !!! assumed to be sitting in the pack switched path!!!
*
*
*********************************************************************/
#if (SCSI_STANDARD_MODE && SCSI_PACKETIZED_IO_SUPPORT)
void SCSIhStandardPackNonPackQueueHIOB (SCSI_HHCB SCSI_HPTR hhcb,
                                        SCSI_HIOB SCSI_IPTR hiob)
{
#if defined(SCSI_INTERNAL)
   SCSI_ASYNC_EVENT_COMMAND(hhcb,hiob,SCSI_AE_PACKTONONPACK_DETECTED);
#else
   SCSI_UNIT_CONTROL SCSI_IPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_UEXACT8 scsiId;
   
   /* Get the target id */
   scsiId = (SCSI_UEXACT8) targetUnit->scsiID;
   /* Generate asynchronous event to the RSM layer to notify that the  */
   /* hw layer detected a pack to non pack switch and to redispatch    */
   /* all pending hiobs in this target's device and host queues.       */
   /*Remove SCBs from HW queues*/
      
   SCSI_hFREEZEHWQUEUE(hhcb,hiob,SCSI_AE_PACKTONONPACK_DETECTED);

   SCSI_ASYNC_EVENT_COMMAND(hhcb,hiob,SCSI_AE_PACKTONONPACK_END);
#endif /* defined(SCSI_INTERNAL) */
}
#endif /* (SCSI_STANDARD_MODE &&  SCSI_PACKETIZED_IO_SUPPORT) */




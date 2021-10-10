/*$Header: /vobs/u320chim/src/chim/hwm/hwmutil.c   /main/196   Thu Aug 21 17:31:13 2003   luu $*/
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
*  Module Name:   HWMUTIL.C
*
*  Description:
*                 Codes to implement utility for hardware management module
*
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         Utilities defined in this file should be generic and
*                 primitive.
*
*  Entry Point(s):
*                 SCSIHReadScbRAM
*                 SCSIHWriteScbRAM
*                 SCSIHHardwareInResetState
*                 SCSIHReadScratchRam
*                 SCSIHWriteScratchRam
*                 SCSIHDataBusHang
*                 SCSIHApplyNVData
*                 SCSIHBiosState
*                 SCSIHRealModeExist
*                 SCSIHUpdateDeviceTable
*
***************************************************************************/

#include "scsi.h"
#include "hwm.h"

/*********************************************************************
*
*  SCSIhPauseAndWait
*
*     Pause and wait for pauseack
*
*  Return Value:  none
*
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
void SCSIhPauseAndWait (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

   /* The SCSI_hWRITE_HCNTRL macro will handle the wait for PAUSEACK. */
   SCSI_hWRITE_HCNTRL(hhcb, OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) | SCSI_PAUSE);
}

/*********************************************************************
*
*  SCSIhUnPause
*
*     UnPause chip
*
*  Return Value:  none
*
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/

void SCSIhUnPause (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

   SCSI_hWRITE_HCNTRL(hhcb, OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & ~SCSI_PAUSE);
}


/*********************************************************************
*
*  SCSIhGetModeRegister
*
*     Get the current value of the MODE_PTR register
*
*  Return Value:  contents of MODE_PTR register
*                  
*  Parameters:    hhcb
*
*  Remarks:       Sets the hhcb field currentMode to register value
*                 read. 
*                  
*********************************************************************/
#if !SCSI_BIOS_SUPPORT
SCSI_UEXACT8 SCSIhGetModeRegister (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

   hhcb->SCSI_HP_currentMode =
      OSD_INEXACT8(SCSI_AICREG(SCSI_MODE_PTR));

   return(hhcb->SCSI_HP_currentMode);
} 
#endif /* !SCSI_BIOS_SUPPORT */

/*********************************************************************
*
*  SCSIhSetModeRegister
*
*     Write to SCSI_MODE_PTR register
*
*  Return Value:  0 - successful
*                 SCSI_ASIC_FAILURE - failure
*                  
*  Parameters:    hhcb
*                 mode - value to be written
*
*  Remarks:       Potential delay in writing SCSI_MODE_PTR register
*                 means that the value to be written must be verified
*                 correct by reading the register before returning. 
*                  
*********************************************************************/
SCSI_UEXACT8 SCSIhSetModeRegister (SCSI_HHCB SCSI_HPTR hhcb,
                                   SCSI_UEXACT8 mode)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 status = 0;
   SCSI_UINT32 i = 0;

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_MODE_PTR), mode);

#if !SCSI_DCH_U320_MODE
   if (SCSI_hHARPOON_REV_A_AND_B(hhcb))
   {
      /* Need to verify mode written correctly. */   
      while (i < SCSI_REGISTER_TIMEOUT_COUNTER)
      {
         if (OSD_INEXACT8(SCSI_AICREG(SCSI_MODE_PTR)) == mode)
         {
            SCSI_hSETCURRENTMODE(hhcb,mode);
            return(status);
         }
         else
         {
            /* 5 microsecond delay */
            OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
            i++;
         }
      }

      /* Return failure. */ 
      return(SCSI_ASIC_FAILURE);
   }
   else
#endif /* !SCSI_DCH_U320_MODE */
   {
      hhcb->SCSI_HP_currentMode = mode;
      return(status);
   }
}
 
/*********************************************************************
*
*  SCSIhDisableDataChannel
*
*     Disable the Data Channel 
*
*  Return Value:  0 - Data Channel successfully disabled.
*                 non-zero - Data Channel not disabled within
*                            timelimit.  
*
*  Parameters:    hhcb
*                 mode - indicates which channel to disable 
*
*  Remarks:       The HWM must wait for HDMAENACK and SCSIENACK
*                 in DFCNTRL to be read as 0 after disabling the
*                 data channel.    
*          
*                 The SCSI_MODE_PTR register is set to channel
*                 being disabled.
*                  
*********************************************************************/
SCSI_UEXACT8 SCSIhDisableDataChannel (SCSI_HHCB SCSI_HPTR hhcb,
                                      SCSI_UEXACT8 mode)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UINT32 i; 
#if !SCSI_DCH_U320_MODE      
   SCSI_UEXACT8 dfcntrl;
#endif /* !SCSI_DCH_U320_MODE */      

   SCSI_hSETMODEPTR(hhcb,scsiRegister,mode);

#if !SCSI_DCH_U320_MODE      
   dfcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL));

   /* Check to see if transfers are enabled bewteen the SCSI bus and
    * the data FIFO.
    * Harpoon2 Rev A has an issue that may result in infinite PCI retries
    * if the data channel is not shut down gracefully (when data transfer
    * is using DT Data group transfers). Therefore, disable
    * SCSI before disabling DMA engine. Note the same algorithm is used
    * for Rev B although it should not be required.
    */ 
   if (dfcntrl & SCSI_SCSIENACK)
   {
      /* Turn off the SCSI transfer */
      /* Warning; Rev B has a bug that results in the SCSIENACK bit
       * never turning off if DT datagroup transfers are active.  
       * If the SCSIEN bit is reset right after the assertion of 
       * an ACK (the leading edge of the outgoing strobe),
       * the logic will not reset the SCSIENACK bit. Therefore,
       * we should not loop for SCSI_REGISTER_TIMEOUT_COUNTER on
       * this register bit.
       */
      i = 0;
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFCNTRL), dfcntrl & ~SCSI_SCSIENACK);
      while (((dfcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL))) & SCSI_SCSIENACK) &&
             (i++ < (SCSI_UINT32)5))
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFCNTRL), dfcntrl & ~SCSI_SCSIENACK);
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }
   }

   /* Now check if transfers enabled between PCI/PCI-X bus and data FIFO */
   dfcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL));
   if (dfcntrl & (SCSI_HDMAENACK | SCSI_SCSIENACK))
   {
      /* Turn off DMA engine */
      i = 0;
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFCNTRL),
                    dfcntrl & (~(SCSI_HDMAENACK | SCSI_SCSIENACK)));

      /* Only wait for HDMAENACK to clear as SCSIENACK may never clear. */ 
      while (((dfcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL))) &
              (SCSI_HDMAENACK)) &&
             (i++ < (SCSI_UINT32)SCSI_REGISTER_TIMEOUT_COUNTER))
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFCNTRL),
                       dfcntrl & (~(SCSI_HDMAENACK | SCSI_SCSIENACK)));

         /* If SCSI_HDONE is set, then host dma just finishes some transfer.  */ 
         /* The host dma engine has stopped, no need to wait for dmaenack     */ 
         /* to be cleared.                                                    */ 
         /* This is to cover a problem that hdmaenack bit won't get cleared   */ 
         /* right after Harpoon2 chip finishes a retry transaction on PCI bus.*/ 
         if (OSD_INEXACT8(SCSI_AICREG(SCSI_DFSTATUS)) & SCSI_HDONE)
         {
            return(0);
         }

         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }
   }
#else
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFCNTRL),(SCSI_RSTSCSIEN | SCSI_RSTHDMAEN));
   /* Must wait on HDMAENACK deasserted before continuing,
    * otherwise the hardware may end up generating an
    * invalid DMA request(such as a DMA write of address 0).
    */
   i = 0;
   while ((OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL)) &
           (SCSI_HDMAENACK | SCSI_SCSIENACK)) &&
          (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
   {
      OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
   }
#endif /* !SCSI_DCH_U320_MODE */

   return((OSD_INEXACT8(SCSI_AICREG(SCSI_DFCNTRL)) & 
           (SCSI_HDMAENACK | SCSI_SCSIENACK)));
}

/*********************************************************************
*
*  SCSIhDisableBothDataChannels
*
*     Disable both Data Channels 
*
*  Return Value:  0 - Data Channels successfully disabled.
*                 non-zero - At least one Data Channel not disabled
*                            within timelimit.  
*
*  Parameters:    hhcb
*
*  Remarks:       This routine must only to be called after a 
*                 3rd party reset and I/O error interrupt.
*                 The algorithm is specific to handling these
*                 errors. Assumes the channel are reset later
*                 (SCSIhResetChannelHardware is called). 
*
*                 Side effect is SCSISEQ0/1 registers are zeroed.
*                 Caller may want to restore SCSISEQ0/1.
*                  
*********************************************************************/
SCSI_UEXACT8 SCSIhDisableBothDataChannels (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 i; 
   SCSI_UEXACT8 dffstat;
   SCSI_UEXACT8 retValue;
   SCSI_UEXACT8 savedMode;

   retValue = 0;

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Switch to mode 3 - a known mode for SCBPTR access. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   /* Disable selection in/out and reselection logic */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ0), 0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISEQ1), 0);

   /* Determine which data FIFO is currently connected to the SCSI bus
    * We should shut down the channel that wasn't connected first. Note 
    * that; this handling is different from Reset initiated case.
    */
   dffstat = OSD_INEXACT8(SCSI_AICREG(SCSI_DFFSTAT));
   for (i = 0; i < (SCSI_UEXACT8) 2 ; i++)
   {
      /* Disable Channel that wasn't connected 1st */
      if (dffstat & SCSI_CURRFIFO)
      {
         /* Disable Mode 0 channel */
         if (SCSIhDisableDataChannel(hhcb,(SCSI_UEXACT8)SCSI_MODE0))
         {
            retValue = 1;
         }

         /* Switch current FIFO */
         dffstat &= ~SCSI_CURRFIFO;
      }
      else
      {
         /* Disable Mode 1 channel */
         if (SCSIhDisableDataChannel(hhcb,(SCSI_UEXACT8)SCSI_MODE1))
         {
            retValue = 1;
         }

         /* Switch current FIFO */
         dffstat |= SCSI_CURRFIFO;
      }

      if (i == 0)
      {
         /* Switch to mode 3. */
         SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

         /* Set CURRFIFO bit to the other channel not connected such that  */
         /* we can shut down DMA on the channel currently connected.       */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFFSTAT), dffstat);
      }
   }

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);
   
   return(retValue);
}

/*********************************************************************
*
*  SCSIhWait4Req
*
*     Wait for target to assert REQ.
*
*  Return Value:  current SCSI bus phase
*                 -1 - error indication
*
*  Parameters     hhcb
*
*  Remarks:       bypasses sequencer
*
*                 Assume we are in Mode 3.
*
*********************************************************************/
SCSI_UEXACT8 SCSIhWait4Req (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 sstat1;
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 scsidatl;
   SCSI_UINT32 i = 0;
   SCSI_UINT32 count;

   /* The usual 0x800000 count has been reduced to 0x80000, as 0x800000 turns */
   /* out to be more than 2 minutes for this outer loop in Jalapeno board. */
   count = 0x80000;
   
   while(1)
   {
      /* Bus is stuck and we cannot clear error */
      if (count-- == 0)
      {
         return((SCSI_UEXACT8)-1);
      }

      i = 0;

      while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_ACKI) &&
             (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
      {
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }

#if SCSI_PACKETIZED_IO_SUPPORT
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & SCSI_STRB2FAST)
      {
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID39);
      }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

      i = 0;
      while (((sstat1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1))) & SCSI_REQINIT) == 0)
      {
         if (((sstat1 & (SCSI_BUSFREE | SCSI_SCSIRSTI)) &&
              (OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT)) & SCSI_SCSIINT)) ||
             (i++ >= SCSI_REGISTER_TIMEOUT_COUNTER))  /* time out check */
         {
            return((SCSI_UEXACT8)-1);
         }
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }

      phase = (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_BUSPHASE);

      /* If SCSI phase is MESSAGE IN or STATUS phase and SCSI parity  */
      /* error set, then CHIM will ACK the byte.                      */
      /* This routine might be called when there is a parity error    */
      /* pending in MESSAGE IN or STATUS phase.                       */
      if (((phase == SCSI_MIPHASE) || (phase == SCSI_STPHASE)) &&
          (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & SCSI_SCSIPERR))
      {
         /* Clear SCSIPERR inorder to ACK the message/status byte */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRSCSIPERR);
         scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
      }
      else
      {
         break;
      }
   }
   return(phase);
}

/*********************************************************************
*
*  SCSIhDelayCount25us
*
*     Delay multiple of 25 uSec
*
*  Return Value:  none
*
*  Parameters     hhcb
*                 count ( each count represent 25 uSec )
*
*  Remarks:       1. This is a blocking routine. It will return to
*                    caller only after the whole delay has been completed.
*
*                 2. U320 ASIC only provide 16 bit timer. For delay that
*                    need to be longer than 1.638375 Seconds, the timer
*                    will be programmed multiple times.
*
*                 3. Sequencer will be paused during the delay.
*                    If sequencer ever use the same s/w timer mechanism,
*                    we'll need arbitration logic to share the built-in
*                    timer usage.
*
*                 4. This routine has no MODE_PTR requirements. All
*                    registers accessed in this routine may be accessed
*                    in any mode.
*
*********************************************************************/
void SCSIhDelayCount25us (SCSI_HHCB SCSI_HPTR hhcb,
                          SCSI_UEXACT32 count)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;
#if !SCSI_DCH_U320_MODE
   SCSI_UEXACT8 byteValue;
   SCSI_UEXACT8 byte1Count;
   SCSI_UEXACT8 byte0Count;
#else
   SCSI_UEXACT8 savedMode;
   SCSI_UEXACT16 savedtick;
#endif /* !SCSI_DCH_U320_MODE */
   if (!count)
   {
      return;  /* use this for deskewing */
   }               
   
#if SCSI_SIMULATION_SUPPORT
   /* Setting delay count to a minimum value exercise the timer logic */
   /* without wasting too much waiting time during ASIC simulation.   */
   count = 1;
#endif /* SCSI_SIMULATION_SUPPORT */

   /* Pause SEQ if necessary */
   if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

#if SCSI_DCH_U320_MODE
   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 4  for timer controls. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

   /* make sure the timer is stopped. */
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_HST_TIMER_CTRL)) & SCSI_HST_TIMERRUN)
   {
      /* Stop timer count */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_HST_TIMER_CTRL), SCSI_HST_TIMERSTOP);
   }
   
   /* Set tick value if necessary */

   savedtick = OSD_INEXACT16(SCSI_AICREG(SCSI_HST_TIMER_TICK));
   if (savedtick != SCSI_HST_TIMER_DEFAULT)
   
   {
      OSD_OUTEXACT16(SCSI_AICREG(SCSI_HST_TIMER_TICK), SCSI_HST_TIMER_DEFAULT);
   }
   
   
   /* set timer count */
   OSD_OUTEXACT16(SCSI_AICREG(SCSI_HST_TIMER_COUNT),0x0001);
   
   /* Start timer */
      
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_HST_TIMER_CTRL), SCSI_HST_TIMERSTART);
   
   /* wait for timer to expire */
   
   while (OSD_INEXACT8(SCSI_AICREG(SCSI_HST_TIMER_CTRL)) & SCSI_HST_TIMERRUN);


   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  

#else      
   byte1Count = (SCSI_UEXACT8)((count&0x0000FF00)>>8);
   byte0Count = (SCSI_UEXACT8) (count&0x000000FF);

   do
   {
      /* The h/w timer has a 16bit counter. Each count represent 25uSec. */
      /* This limit a maximum one time delay of 1.638375 Sec.            */
      /* For longer delay e.g., 2 Seconds delay, the h/w timer need      */
      /* to be programmed more than once.                                */

      if (count>0x0000FFFF)
      {
         byte0Count=byte1Count=0xFF;
         count -= 0x0000FFFF;
      }
      else
      {
         byte1Count = (SCSI_UEXACT8)((count&0x0000FF00)>>8);
         byte0Count = (SCSI_UEXACT8) (count&0x000000FF);
         count=0;
      }
      
      /* Clears all timer timeout flags from ARPINSTAT & HSTINSTAT */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRARPINTSTAT),
                    SCSI_CLRARP_SWTMRTO);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), SCSI_CLRSWTMINT);

      /* Set timer count */
      /* NOTE: SCSI_SWTIMER register is a 16 bit register. However it must be */
      /*       accessed using two 8bit operation.                             */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SWTIMER0), byte0Count);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SWTIMER1), byte1Count);

      /* This delay routine poll for timer expiration. */
      /* Set timer notification direction to HOST & don't generate INT/MSI. */
      /* Start the timer */
      byteValue = OSD_INEXACT8(SCSI_AICREG(SCSI_INTCTL));
      byteValue = (byteValue & ~SCSI_SWTMINTEN) | SCSI_SWTMINTMASK | SCSI_SWTIMER_START;

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_INTCTL), byteValue);

      while (1)
      {
         if (OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT)) & SCSI_SWTMINT)
         {
            /* Timer expired */
            break; 
         }
      } 
        
   } while (count > 0);

   /* clear timer interrupt */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), SCSI_CLRSWTMINT);
   
#endif /* SCSI_DCH_U320_MODE */
   /* Unpause SEQ, if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb,hcntrl);
   }
}

/*********************************************************************
*
*  SCSIhSetupAssignScbBuffer
*
*     Setup for assign scb buffer from builtin scb buffer pool
*
*  Return Value:  none
*
*  Parameters     hhcb
*
*  Remarks:       
*
*********************************************************************/
#if ((!SCSI_BIOS_SUPPORT) && SCSI_SCBBFR_BUILTIN)
void SCSIhSetupAssignScbBuffer (SCSI_HHCB SCSI_HPTR hhcb)
{
#if SCSI_SIMULATION_SUPPORT
#if OSD_BUS_ADDRESS_SIZE == 32
   hhcb->SCSI_SCB_BUFFER.busAddress = OSD_GET_BUS_ADDRESS(hhcb,
               SCSI_MC_LOCKED,hhcb->SCSI_SCB_BUFFER.virtualAddress);
#endif /* OSD_BUS_ADDRESS_SIZE == 32 */
#if OSD_BUS_ADDRESS_SIZE == 64
   hhcb->SCSI_SCB_BUFFER.busAddress = (SCSI_BUS_ADDRESS) (hhcb->SCSI_SCB_BUFFER.virtualAddress);
#endif /* OSD_BUS_ADDRESS_SIZE == 64 */
#else 
#if (!SCSI_EFI_BIOS_SUPPORT && !SCSI_ASPI_SUPPORT)

   hhcb->SCSI_SCB_BUFFER.busAddress = OSD_GET_BUS_ADDRESS(hhcb,
               SCSI_MC_LOCKED,hhcb->SCSI_SCB_BUFFER.virtualAddress);

#endif /* !SCSI_EFI_BIOS_SUPPORT && !SCSI_ASPI_SUPPORT */
#if SCSI_ASPI_SUPPORT

    OSD_GET_BUS_ADDRESS(hhcb,
               SCSI_MC_LOCKED,hhcb->SCSI_SCB_BUFFER.virtualAddress);
    hhcb->SCSI_SCB_BUFFER.busAddress = hhcb->busAddress; 
#endif /* SCSI_ASPI_SUPPORT */
#endif /* SCSI_SIMULATION_SUPPORT */
   hhcb->SCSI_SCB_BUFFER.bufferSize =
      SCSI_hSIZE_OF_SCB_BUFFER(hhcb->firmwareMode) * hhcb->numberScbs;
}
#endif /* ((!SCSI_BIOS_SUPPORT) && SCSI_SCBBFR_BUILTIN) */

/*********************************************************************
*
*  SCSIhAssignScbDescriptor
*
*     Assign scb descriptor from builtin scb buffer pool
*
*  Return Value:  none
*
*  Parameters     hhcb
*                 hiob
*
*  Remarks:       
*
*********************************************************************/
#if ((!SCSI_BIOS_SUPPORT) && SCSI_SCBBFR_BUILTIN)
void SCSIhAssignScbDescriptor (SCSI_HHCB SCSI_HPTR hhcb,
                               SCSI_HIOB SCSI_IPTR hiob)
{
   hiob->scbDescriptor = hhcb->SCSI_FREE_HEAD;
   hhcb->SCSI_FREE_HEAD = hhcb->SCSI_FREE_HEAD->queueNext;
   hiob->scbDescriptor->scbNumber = hiob->scbNumber;
}
#endif /* ((!SCSI_BIOS_SUPPORT) && SCSI_SCBBFR_BUILTIN) */

/*********************************************************************
*
*  SCSIhFreeScbDescriptor
*
*     Assign scb descriptor from builtin scb buffer pool
*
*  Return Value:  none
*
*  Parameters     hhcb
*                 hiob
*
*  Remarks:       
*
*********************************************************************/
#if ((!SCSI_BIOS_SUPPORT) && SCSI_SCBBFR_BUILTIN)
void SCSIhFreeScbDescriptor (SCSI_HHCB SCSI_HPTR hhcb,
                             SCSI_HIOB SCSI_IPTR hiob)
{
   /* Return the SCB buffer to the free queue */
   hhcb->SCSI_FREE_TAIL->queueNext = hiob->scbDescriptor;
   hhcb->SCSI_FREE_TAIL = hiob->scbDescriptor;

   /* Nullify scbNumber of the returned SCB buffer as it is no longer */
   /* associated with this SCB buffer */
   SCSI_SET_NULL_SCB(hhcb->SCSI_FREE_TAIL->scbNumber);
}
#endif /* ((!SCSI_BIOS_SUPPORT) && SCSI_SCBBFR_BUILTIN) */

/*********************************************************************
*
*  SCSIHReadScbRAM
*
*     Read from scb RAM hardware
*
*  Return Value:  none
*
*  Parameters     hhcb
*                 data buffer to be filled
*                 scb number
*                 offset within specified scb
*                 length of bytes to be read
*
*  Remarks:       This routine is designed for OSD to access scb
*                 hardware memory. OSD must have special knowledge
*                 about information stored in scb ram. Usually this
*                 routine is required only for software which is
*                 compatible with ADAPTEC BIOS.
*
*********************************************************************/
void SCSIHReadScbRAM (SCSI_HHCB SCSI_HPTR hhcb,
                      SCSI_UEXACT8 SCSI_SPTR dataBuffer,
                      SCSI_UEXACT16 scbNumber,
                      SCSI_UEXACT8 scbOffset,
                      SCSI_UEXACT8 byteLength)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 i;
   SCSI_UEXACT8 savedMode;

   /* Save the HCNTRL value. */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* Pause the chip, if necessary. */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 - a known mode for SCBPTR access. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
   /* Save mode 3 SCBPTR value. */
   scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
            ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

   /* Set SCBPTR to scbNumber. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbNumber);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                 (SCSI_UEXACT8)(scbNumber >> 8));

   /* Read from scb RAM. */
   for (i = 0; i < byteLength; i++)
   {
      dataBuffer[i] = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+scbOffset+i));
   }

   /* Restore mode 3 SCBPTR. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                 (SCSI_UEXACT8)(scbptr >> 8));

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  
   
   /* Restore HCNTRL, if necessary. */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
}

/*********************************************************************
*
*  SCSIHWriteScbRAM
*
*     Write to scb RAM hardware
*
*  Return Value:  none
*
*  Parameters     hhcb
*                 scb number
*                 offset within specified scb
*                 length of bytes to be read
*                 data buffer to be filled
*
*  Remarks:       This routine is designed for OSD to access scb
*                 hardware memory. OSD must have special knowledge
*                 about information stored in scb ram. Usually this
*                 routine is required only for software which is
*                 compatible with ADAPTEC BIOS.
*
*********************************************************************/
#if (SCSI_ACCESS_RAM || SCSI_BIOS_SUPPORT)
void SCSIHWriteScbRAM (SCSI_HHCB SCSI_HPTR hhcb,
                       SCSI_UEXACT16 scbNumber,
                       SCSI_UEXACT8 scbOffset,
                       SCSI_UEXACT8 byteLength,
                       SCSI_UEXACT8 SCSI_SPTR dataBuffer)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT8 i;
   SCSI_UEXACT8 savedMode;

   /* Save the HCNTRL value. */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* Pause the chip, if necessary. */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 - a known mode for SCBPTR access. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
   /* Save mode 3 SCBPTR value. */
   scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
            ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

   /* Set SCBPTR to scbNumber. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbNumber);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                 (SCSI_UEXACT8)(scbNumber >> 8));

   /* Write to scb RAM. */
   for (i = 0; i < byteLength; i++)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+scbOffset+i),dataBuffer[i]);
   }

   /* Restore mode 3 SCBPTR. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)scbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                 (SCSI_UEXACT8)(scbptr >> 8));

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  

   /* restore HCNTRL if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
}
#endif /* (SCSI_ACCESS_RAM || SCSI_BIOS_SUPPORT) */

/*********************************************************************
*
*  SCSIHHardwareInResetState
*
*     Check if harwdare is in chip reset state
*
*  Return Value:  1, chip is in reset state
*                 0, chip is not in reset state
*
*  Parameters     hhcb
*
*  Remarks:       This routine is designed for upper layer to check
*                 if hardware has ever been initialized. It's typically
*                 used internallly by upper layers implemented by
*                 Adaptec.
*
*********************************************************************/
#if (SCSI_ACCESS_RAM && !SCSI_BIOS_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
SCSI_INT SCSIHHardwareInResetState (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   
#if !SCSI_DCH_U320_MODE
   if (!(OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & SCSI_CHIPRST))
#else      
   if (!(OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & SCSI_DCHRST))
#endif /* !SCSI_DCH_U320_MODE */
   { 
      return(hhcb->SCSI_HF_chipReset);
   }
   else
   {
      return(1);
   }
}
#endif /* (SCSI_ACCESS_RAM && !SCSI_BIOS_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */

/*********************************************************************
*
*  SCSIHReadScratchRam
*
*     Read from scratch ram hardware
*
*  Return Value:  none
*
*  Parameters     hhcb
*                 dataBuffer
*                 offset
*                 length
*
*  Remarks:       This routine is designed for upper layer to check
*                 if hardware has ever been initialized. It's typically
*                 used internallly by upper layers implemented by
*                 Adaptec.
*
*********************************************************************/
#if !SCSI_ASPI_REVA_SUPPORT  
void SCSIHReadScratchRam (SCSI_HHCB SCSI_HPTR hhcb,
                          SCSI_UEXACT8 SCSI_SPTR dataBuffer,
                          SCSI_UEXACT16 offset,
                          SCSI_UEXACT16 length)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT16 i;
   SCSI_UEXACT16 scratchAddress = 0;

   /* save the HCNTRL value */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* pause the chip if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   if (offset < SCSI_SCRATCH0_SIZE)
   {
      scratchAddress = (SCSI_UEXACT16)SCSI_SCRATCH0 + (SCSI_UEXACT16)offset;
   }

   /* read from scratch RAM */
   for (i = 0; i < length; i++)
   {
      dataBuffer[i] = OSD_INEXACT8_HIGH(SCSI_AICREG(scratchAddress+i));
   }

   /* restore HCNTRL if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
}
#endif /* !SCSI_ASPI_REVA_SUPPORT */

/*********************************************************************
*
*  SCSIHWriteScratchRam
*
*     Write to scratch ram hardware
*
*  Return Value:  none
*
*  Parameters     hhcb
*                 offset
*                 length
*                 dataBuffer
*
*  Remarks:       This routine is designed for upper layer to check
*                 if hardware has ever been initialized. It's typically
*                 used internallly by upper layers implemented by
*                 Adaptec.
*
*********************************************************************/
#if !SCSI_ASPI_REVA_SUPPORT   
void SCSIHWriteScratchRam (SCSI_HHCB SCSI_HPTR hhcb,
                           SCSI_UEXACT16 offset,
                           SCSI_UEXACT16 length,
                           SCSI_UEXACT8 SCSI_SPTR dataBuffer)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT16 scratchAddress = 0;
   SCSI_UEXACT16 i;

   /* save the HCNTRL value */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* pause the chip if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   if (offset < SCSI_SCRATCH0_SIZE)
   {
      scratchAddress = (SCSI_UEXACT16)SCSI_SCRATCH0 + (SCSI_UEXACT16)offset;
   }

   /* write to scratch RAM */
   for (i = 0; i < length; i++)
   {
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(scratchAddress+i),dataBuffer[i]);
   }

   /* restore HCNTRL if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
}
#endif /* !SCSI_ASPI_REVA_SUPPORT */

/*********************************************************************
*
*  SCSIhWriteHCNTRL
*
*     Implement required for write to HCNTRL register
*
*  Return Value:  0 - successful
*                 1 - failure
*                  
*  Parameters:    hhcb
*                 value to be written
*
*  Remarks:       Potential of post write for memory mapped IO and
*                 internal signal synchronization that might cause
*                 misinterpretation of the paused/unpaused chip on
*                 the fast CPU and PCI (66MHz) speed system.  This
*                 happens on the back-to-back HCNTRL access.
*                  
*********************************************************************/
SCSI_UEXACT8 SCSIhWriteHCNTRL (SCSI_HHCB SCSI_HPTR hhcb,
                               SCSI_UEXACT8 value)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 status;
   SCSI_UINT32 i = 0;

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_HCNTRL), value);

   if (value & SCSI_PAUSE)
   {
      /* make sure the chip is paused before proceed */
      while ((!(OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & SCSI_PAUSEACK)) &&
             (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
      {
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }    
   }
   else
   {
      /* make sure the chip is unpaused before proceed */
      while ((OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL)) & SCSI_PAUSEACK) &&
             (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
      {
         /* we should get out the loop if the chip is paused due to */
         /* interrupt after an unpause. */
         if (OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT)) &
             (SCSI_BRKADRINT | SCSI_SCSIINT | SCSI_ARPINT))
         {
            break;
         }
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }    
   }

   status = (i >= SCSI_REGISTER_TIMEOUT_COUNTER) ? 1 : 0;

   return(status);      
}

/*********************************************************************
*
*  SCSIhSetAddressScbBA32
*
*     Translate virtual address to 32 bits bus address and
*     set the 32 bits bus address to scb ram
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 scb address
*                 virtual address
*
*  Remarks:       This routine is mostly called during building a
*                 request sense commnand.  The SCB pointer register
*                 should point to the correct SCB ram.
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 32) && (!SCSI_BIOS_MODE_SUPPORT))
void SCSIhSetAddressScbBA32 (SCSI_HHCB SCSI_HPTR hhcb,
                             SCSI_UEXACT16 scbAddress,
                             void SCSI_HPTR virtualAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;

   /* get bus address */
#if SCSI_ASPI_SUPPORT 
   OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,virtualAddress);
   busAddress = hhcb->busAddress;
#else
   busAddress = OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,virtualAddress);
#endif  /* SCSI_ASPI_SUPPORT */
   /* load bus address to scb ram */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+0),((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+1),((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order1);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+2),((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order2);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+3),((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order3);
}
#endif /* ((OSD_BUS_ADDRESS_SIZE == 32) && (!SCSI_BIOS_MODE_SUPPORT)) */

/*********************************************************************
*
*  SCSIhSetAddressScbBA64
*
*     Translate virtual address to 64 bits bus address and
*     set the 64 bits bus address to scb ram
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 scb address
*                 virtual address
*
*  Remarks:       This routine is mostly called during building a
*                 request sense commnand.  The SCB pointer register
*                 should point to the correct SCB ram.
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 64) && (!SCSI_BIOS_MODE_SUPPORT))
void SCSIhSetAddressScbBA64 (SCSI_HHCB SCSI_HPTR hhcb,
                             SCSI_UEXACT16 scbAddress,
                             void SCSI_HPTR virtualAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;

   /* get bus address */
#if SCSI_ASPI_SUPPORT 
   OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,virtualAddress);
   busAddress = hhcb->busAddress;
#else
   busAddress = OSD_GET_BUS_ADDRESS(hhcb,SCSI_MC_LOCKED,virtualAddress);
#endif /* SCSI_ASPI_SUPPORT */
   /* load bus address to scb ram */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+0),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+1),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order1);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+2),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order2);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+3),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order3);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+4),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order4);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+5),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order5);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+6),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order6);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+7),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order7);
}
#endif /* ((OSD_BUS_ADDRESS_SIZE == 64) && (!SCSI_BIOS_MODE_SUPPORT)) */

/*********************************************************************
*
*  SCSIhSetAddressScbBA32
*
*     
*     Set the 32 bits bus address to scb ram
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 scb address
*                 physicalAddress
*
*  Remarks:       This routine is mostly called during building a
*                 request sense commnand.  The SCB pointer register
*                 should point to the correct SCB ram.
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 32) && (SCSI_BIOS_MODE_SUPPORT))
void SCSIhSetAddressScbBA32 (SCSI_HHCB SCSI_HPTR hhcb,
                             SCSI_UEXACT16 scbAddress,
                             SCSI_BUS_ADDRESS physicalAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;

   /* get bus address */
   busAddress = physicalAddress;

   /* load bus address to scb ram */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+0),((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+1),((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order1);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+2),((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order2);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+3),((SCSI_QUADLET SCSI_IPTR)&busAddress)->u8.order3);
}
#endif /* ((OSD_BUS_ADDRESS_SIZE == 32) && (SCSI_BIOS_MODE_SUPPORT)) */

/*********************************************************************
*
*  SCSIhSetAddressScbBA64
*
*     Translate virtual address to 64 bits bus address and
*     set the 64 bits bus address to scb ram
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 scb address
*                 physical address
*
*  Remarks:       This routine is mostly called during building a
*                 request sense commnand.  The SCB pointer register
*                 should point to the correct SCB ram.
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 64) && (SCSI_BIOS_MODE_SUPPORT))
void SCSIhSetAddressScbBA64 (SCSI_HHCB SCSI_HPTR hhcb,
                             SCSI_UEXACT16 scbAddress,
                             SCSI_BUS_ADDRESS physicalAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;

   /* get bus address */
   busAddress = physicalAddress;

   /* load bus address to scb ram */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+0),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order0);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+1),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order1);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+2),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order2);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+3),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order3);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+4),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order4);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+5),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order5);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+6),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order6);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(scbAddress+7),((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order7);
}
#endif /* #if ((OSD_BUS_ADDRESS_SIZE == 64) && (SCSI_BIOS_MODE_SUPPORT)) */

/*********************************************************************
*
*  SCSIhGetAddressScbBA32
*
*     Translate virtual address to 32 bits bus address and
*     set the 32 bits bus address to scb ram
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 scb address
*                 virtual address
*
*  Remarks:       This routine is mostly called during a residual 
*                 length calculation.  The SCB pointer register
*                 should point to the correct SCB ram.
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 32) && !(SCSI_ASPI_SUPPORT))
SCSI_BUS_ADDRESS SCSIhGetAddressScbBA32 (SCSI_HHCB SCSI_HPTR hhcb,
                                         SCSI_UEXACT16 scbAddress)
{
   return((SCSI_BUS_ADDRESS)SCSIhGetEntity32FromScb(hhcb,scbAddress));
}
#endif /* ((OSD_BUS_ADDRESS_SIZE == 32) && !(SCSI_ASPI_SUPPORT) */


/*********************************************************************
*
*  SCSIhGetAddressScbBA32
*
*     Translate virtual address to 32 bits bus address and
*     set the 32 bits bus address to scb ram
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 scb address
*                 virtual address
*
*  Remarks:       This routine is mostly called during a residual 
*                 length calculation.  The SCB pointer register
*                 should point to the correct SCB ram.
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 32) && (SCSI_ASPI_SUPPORT))
void SCSIhGetAddressScbBA32 (SCSI_HHCB SCSI_HPTR hhcb,
                             SCSI_UEXACT16 scbAddress)
{
   SCSI_UEXACT32 value;

   value =SCSIhGetEntity32FromScb(hhcb,scbAddress);
   hhcb->busAddress = (SCSI_BUS_ADDRESS) value;
}
#endif /* ((OSD_BUS_ADDRESS_SIZE == 32) && !(SCSI_ASPI_SUPPORT) */

/*********************************************************************
*
*  SCSIhGetEntity32FromScb
*
*     Get a 32 bit entity from scb ram
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 scb address
*                 virtual address
*
*  Remarks:       This routine is mostly called during a residual 
*                 length calculation.
*                  
*********************************************************************/
SCSI_UEXACT32 SCSIhGetEntity32FromScb (SCSI_HHCB SCSI_HPTR hhcb,
                                       SCSI_UEXACT16 scbAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT32 entity;

   /* load bus address to scb ram */
   ((SCSI_QUADLET SCSI_IPTR)&entity)->u8.order0 = 
         OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+0));
   ((SCSI_QUADLET SCSI_IPTR)&entity)->u8.order1 = 
         OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+1));
   ((SCSI_QUADLET SCSI_IPTR)&entity)->u8.order2 = 
         OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+2));
   ((SCSI_QUADLET SCSI_IPTR)&entity)->u8.order3 = 
         OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+3));

   return(entity);
}

/*********************************************************************
*
*  SCSIhGetAddressScbBA64
*
*     Translate virtual address to 64 bits bus address and
*     set the 64 bits bus address to scb ram
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 scb address
*                 virtual address
*
*  Remarks:       This routine is mostly called during a residual 
*                 length calculation.  The SCB pointer register
*                 should point to the correct SCB ram.
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 64) && (!SCSI_BIOS_SUPPORT) && (!SCSI_ASPI_SUPPORT))
SCSI_BUS_ADDRESS SCSIhGetAddressScbBA64 (SCSI_HHCB SCSI_HPTR hhcb,
                                         SCSI_UEXACT16 scbAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;

   /* load bus address to scb ram */
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order0 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+0));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order1 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+1));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order2 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+2));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order3 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+3));

#if SCSI_STANDARD_MODE 
   if ((hhcb->SCSI_HF_SgBusAddress32) &&
       ((hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320) ||
        (hhcb->firmwareMode == SCSI_FMODE_DCH_U320) ||
        (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320)))
          
   {
      return(busAddress);
   }
#endif /* SCSI_STANDARD_MODE */

   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order4 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+4));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order5 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+5));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order6 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+6));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order7 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+7));

   return(busAddress);
}
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */


/*********************************************************************
*
*  SCSIhGetAddressScbBA64
*
*     Translate virtual address to 64 bits bus address and
*     set the 64 bits bus address to scb ram
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 scb address
*                 virtual address
*
*  Remarks:       This routine is mostly called during a residual 
*                 length calculation.  The SCB pointer register
*                 should point to the correct SCB ram.
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 64) && (!SCSI_BIOS_SUPPORT) && (SCSI_ASPI_SUPPORT))
void SCSIhGetAddressScbBA64 (SCSI_HHCB SCSI_HPTR hhcb,
                             SCSI_UEXACT16 scbAddress)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_BUS_ADDRESS busAddress;

   /* load bus address to scb ram */
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order0 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+0));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order1 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+1));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order2 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+2));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order3 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+3));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order4 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+4));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order5 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+5));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order6 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+6));
   ((SCSI_OCTLET SCSI_IPTR)&busAddress)->u8.order7 = OSD_INEXACT8_HIGH(SCSI_AICREG(scbAddress+7));

   hhcb->busAddress = busAddress;
}
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */

/*********************************************************************
*
*  SCSIhCompareBusAddress
*
*     Compare two bus addresses.
*
*  Return Value:  none
*                  
*  Parameters:    busAddress1
*                 busAddress2
*
*  Remarks:       This routine compares two busAddresses byte by byte.
*                  
*********************************************************************/
SCSI_UINT8 SCSIhCompareBusAddress (SCSI_BUS_ADDRESS busAddress1,
                                   SCSI_BUS_ADDRESS busAddress2)
{
#if OSD_BUS_ADDRESS_SIZE == 32
   /* Compare on the 32-bit bus address */
   if ((((SCSI_QUADLET SCSI_LPTR) &busAddress1)->u16.order0 != 
        ((SCSI_QUADLET SCSI_LPTR) &busAddress2)->u16.order0) ||
       (((SCSI_QUADLET SCSI_LPTR) &busAddress1)->u16.order1 != 
        ((SCSI_QUADLET SCSI_LPTR) &busAddress2)->u16.order1))
   {
      return(1);
   }
#else
   /* Compare on the 64-bit bus address */
   if ((((SCSI_OCTLET SCSI_LPTR) &busAddress1)->u32.order0 !=
        ((SCSI_OCTLET SCSI_LPTR) &busAddress2)->u32.order0) ||
       (((SCSI_OCTLET SCSI_LPTR) &busAddress1)->u32.order1 !=
        ((SCSI_OCTLET SCSI_LPTR) &busAddress2)->u32.order1))
   {
      return(1);
   }
#endif /* OSD_BUS_ADDRESS_SIZE == 32 */
   return(0);
}

/*********************************************************************
*
*  SCSIhResetNegoData
*
*     Reset the SCSI transfer parameters: PERIOD, OFFSET,
*     PPR's protocol option, and WIDE.
*
*  Return Value:  none
*
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
void SCSIhResetNegoData (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 i;
   SCSI_UEXACT8 modeSave;

   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   for (i = 0; i < hhcb->maxDevices; i++)
   {
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
      if (SCSI_hHARPOON_REV_1B(hhcb) &&
          hhcb->SCSI_HF_targetMode &&
          (!hhcb->SCSI_HF_initiatorMode))
      {
         /* Set the upper 4 bits of NEGODAT to our SCSI ID for 
          * multiple target ID support.
          */
         /* Offset to location in auto-rate-offset table by specified target ID */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGOADDR),
                       (i | (hhcb->hostScsiID << 4)));
      }
      else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
      {
         /* Offset to location in auto-rate-offset table by specified target ID */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGOADDR), i);
      }

      /* clear scsi transfer period */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA0), SCSI_ASYNC_XFER_PERIOD);

      /* clear scsi offset */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA1), 0x00);

      /* clear protocol options of PPR */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA2), 0x00);

      /* clear wide bit and set automatic attention out */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA3), SCSI_ENAUTOATNO);

#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
      if (SCSI_hHARPOON_REV_1B(hhcb) &&
          hhcb->SCSI_HF_targetMode &&
          (!hhcb->SCSI_HF_initiatorMode))
      {
         /* Set the upper 4 bits of NEGODAT to our SCSI ID for 
          * multiple target ID support.
          */
         /* Offset to location in auto-rate-offset table by specified target ID */
         /* Reset slew rate / precomp / amp to defaults */
         SCSI_hSETELECINFORMATION(hhcb,
                                  (SCSI_UEXACT8)(i | (hhcb->hostScsiID << 4)),
                                  SCSI_CURRENT_XFER(&SCSI_DEVICE_TABLE(hhcb)[i]));
      }
      else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
      {
         /* Reset slew rate / precomp / amp to defaults */
         SCSI_hSETELECINFORMATION(hhcb,i,SCSI_CURRENT_XFER(&SCSI_DEVICE_TABLE(hhcb)[i]));
      } 
#if SCSI_LOOPBACK_OPERATION
      /* Set the transfer rates to default values */
      SCSIhSetXferRate(hhcb,i);
#endif /* SCSI_LOOPBACK_OPERATION */
   }

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
}

/*********************************************************************
*
*  SCSIhLoadNegoData
*
*     Load the SCSI transfer parameters: PERIOD, OFFSET,
*     PPR's protocol option, and WIDE into NEGODAT registers
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 targetID - if SCSI_MULTIPLEID_SUPPORT is
*                            enabled, the upper 4 bits of this
*                            field may contain 'our' target ID.
*                 transfer rate to be loaded
*
*  Remarks:
*                  
*********************************************************************/
void SCSIhLoadNegoData (SCSI_HHCB SCSI_HPTR hhcb,
                        SCSI_UEXACT8 targetID,
                        SCSI_UEXACT8 SCSI_LPTR xferRate)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 negodata3;
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

   /* Set up the proper precomp, amplitude and slew rate  */
   SCSI_hSETELECINFORMATION(hhcb,targetID,xferRate);

   /* Offset to location in auto-rate-offset table by specified target ID */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGOADDR), targetID);
#if (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)
   if (SCSI_hHARPOON_REV_A(hhcb))
   {
#if !SCSI_DOWNSHIFT_MODE
      if (xferRate[SCSI_XFER_PERIOD] <= 0x08)
      {
         /* The latest SPI-4 specification requires that each data valid   */
         /* state REQ assertion during paced DT data phase corresponds to  */
         /* 4 bytes of data.  But Harpoon2 Rev A chip assumes only 2 bytes */
         /* per REQ assertion.  The workaround is to reduce the maximum    */
         /* offset negotiated by half and programming NEGODAT1 register    */
         /* with twice the negotiated offset.  Razor database entry #560.  */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA1),
               xferRate[SCSI_XFER_OFFSET]*2);
      }
      else
#endif /* !SCSI_DOWNSHIFT_MODE */
      {
         /* Load scsi transfer offset */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA1),xferRate[SCSI_XFER_OFFSET]);
      }

      /* Negate bits that are not defined in the negodat2 reg.*/
      xferRate[SCSI_XFER_PTCL_OPT] &= ~(SCSI_PCOMP_EN | SCSI_RTI | SCSI_RD_STRM |
                                     SCSI_WR_FLOW |SCSI_HOLD_MCS);
   
      /* Load scsi transfer period */

      /* For Harpoon 2 rev A asic: the transfer period factor of 0x07 is meant*/
      /* for 320MB/s speed.  On the ther hand, the SPI-4 spec states that the */
      /* transfer period factor of 0x08 is for 320MB/s speed.                 */
      /* Razor database entry #85. */
      if (xferRate[SCSI_XFER_PERIOD] == 0x08)
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA0), 0x07);
      }
      else
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA0), xferRate[SCSI_XFER_PERIOD]);
      }

      /* Load spi4, QAS, dual edge, and/or packetized bits.           */
      /* Set spi4 bit if transfer period is <= 0x08 (or 320MB/s rate) */
      if (xferRate[SCSI_XFER_PERIOD] <= 0x08)
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA2),
                                    (xferRate[SCSI_XFER_PTCL_OPT] | SCSI_SPI4));
      }
      else
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA2), xferRate[SCSI_XFER_PTCL_OPT]);
      }
   }
   else
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE */

   {
      /* Negate bits that are not defined in the negodat2 reg.*/
      xferRate[SCSI_XFER_PTCL_OPT] &= ~(SCSI_PCOMP_EN | SCSI_RD_STRM |
                                        SCSI_WR_FLOW | SCSI_HOLD_MCS);
      
      /* Load scsi transfer offset */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA1), xferRate[SCSI_XFER_OFFSET]);

      /* Load scsi transfer period */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA0), xferRate[SCSI_XFER_PERIOD]);

      /* Load QAS, dual edge, RTI, and/or packetized bits */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA2), xferRate[SCSI_XFER_PTCL_OPT]);
   }

   negodata3 = OSD_INEXACT8(SCSI_AICREG(SCSI_NEGODATA3));

   if (xferRate[SCSI_XFER_MISC_OPT] & SCSI_WIDE)
   {
      /* Set wide */
      negodata3 |= (SCSI_UEXACT8)SCSI_WIDE;
   }
   else
   {
      /* Clear wide */
      negodata3 &= (SCSI_UEXACT8)(~SCSI_WIDE);
   }

   if (xferRate[SCSI_XFER_PTCL_OPT] & SCSI_PACKETIZED)
   {
      /* Clear AUTOATNO for packetized mode */
      negodata3 &= (SCSI_UEXACT8)(~SCSI_ENAUTOATNO);
   }
   else
   {
      /* Set AUTOATNO for non-packetized mode */
      negodata3 |= (SCSI_UEXACT8)SCSI_ENAUTOATNO;
   }

   /* Clear Asynchronous Information Protection code (ENAIP) */
#if !SCSI_DCH_U320_MODE
   negodata3 &= (SCSI_UEXACT8)(~SCSI_ENAIP);
#if (!SCSI_ASPI_REVA_SUPPORT)    
   if (SCSI_hHARPOON_REV_B(hhcb))
   {
#if SCSI_ENHANCED_U320_SNAPSHOT
      negodata3 |= (SCSI_UEXACT8)(SCSI_SNAPSHOT);
#else
      negodata3 &= (SCSI_UEXACT8)(~SCSI_SNAPSHOT);
#endif /* SCSI_ENHANCED_U320_SNAPSHOT */

#if SCSI_ENHANCED_U320_SLOWCRC
      negodata3 |= (SCSI_UEXACT8)(SCSI_SLOWCRC);
#else
      negodata3 &= (SCSI_UEXACT8)(~SCSI_SLOWCRC);
#endif /* SCSI_ENHANCED_U320_SLOWCRC */


      /* Turn Slow CRC on for U160 transfers */
      /* Certain target firmware will report */
      /* False CRC error with fast CRC */
      /* At this U160 transfer rate */
      if (xferRate[SCSI_XFER_PERIOD] == 0x09)
      {
         negodata3 |= SCSI_SLOWCRC;
      }

   }
#endif /* !SCSI_ASPI_REVA_SUPPORT */   
#endif /* !SCSI_DCH_U320_MODE */
   
   /* Update NEGODATA3 register */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA3), negodata3);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   /* Restore HCNTRL, if necessary. */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
}

/*********************************************************************
*
*  SCSIhClearDataFifo
*
*     Clean up byte in Data Fifo
*
*  Return Value:  0 - Failed due to hardware failure
*                 1 - Succeeded
*
*  Parameters:    hhcb
*                 hiob
*                 originalMode
*
*  Remarks:       Assume we're in originalMode
*                 This routine gets call by SCSIhCheckLength routine
*                 on the data overrun condition for legacy SCSI.
*                           
*********************************************************************/
SCSI_UEXACT8 SCSIhClearDataFifo (SCSI_HHCB SCSI_HPTR hhcb,
                                 SCSI_HIOB SCSI_IPTR hiob,
                                 SCSI_UEXACT8 originalMode)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UINT32 i;                /* time out counter */
   SCSI_UEXACT8 stat = 1;        /* assume fifo cleanup is fine */

   SCSIhResetDataChannel(hhcb,originalMode);

   /* If the data overrun interrupt is after the command complete message  */
   /* has been acked, then CHIM does not need to set up last_seg_done bit. */
   /* Because SCSI bus might be busy for a new connection and the hardware */
   /* might allocate the data channel that just freed up by the CHIM.      */
   /* Hence, CHIM should not touch the free channel.                       */
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_ARPINTCODE)) != SCSI_DATA_OVERRUN_BUSFREE)
   {
      /* Sequencer requires CHIM to clear the overrun byte count out of   */
      /* the fifo for clrchn to work, but not clear the last_seg_done bit */
      /* (bit 0 of sg_cache_ptr register).  Since CHIM clean up the fifo  */
      /* by resetting the channel, this clears the last_seg_done bit.     */
      /* Therefore, CHIM has to program the hardware to generate the      */
      /* last_seg_done bit.  After a data channel gets reset, last_seg    */
      /* bit (bit 1 of sg_cache_ptr register) is set and the host count   */
      /* registers are reset to 0.  CHIM will need only                   */
      /*   1. program DFCNTL register with PRELOADEN and SCSIEN.          */
      /*   2. waits for the last_seg_done bit set.                        */

      /* Enable preload to active */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_DFCNTRL), (SCSI_PRELOADEN | SCSI_SCSIEN));

      /* Wait for last_seg_done to set */
      i = 0;
      while ((!(OSD_INEXACT8(SCSI_AICREG(SCSI_SG_CACHEPTR)) & SCSI_LAST_SEG_DONE)) &&
             (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
      {
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }

      if (i >= SCSI_REGISTER_TIMEOUT_COUNTER)
      {
         /* Terminate the I/O with an appropriate error. */
         hiob->residualLength = 0;      
         hiob->haStat = SCSI_HOST_DU_DO;
         SCSIhTerminateCommand(hiob);

         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID33);
         stat = 0;
      }
   }

   return(stat);
}

/*********************************************************************
*
*  SCSIhResetDataChannel
*
*     Reset the Data channel specified 
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 modePtr - point to channel to be cleared
*
*  Remarks:       This routine may not be complete at this time.
*                 Needs to be revisited.
*                 The SCSI_MODE_PTR register is set to channel.
*                           
*********************************************************************/
void SCSIhResetDataChannel (SCSI_HHCB SCSI_HPTR hhcb,
                            SCSI_UEXACT8 modePtr)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 value;

   /* Set mode to specific channel */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modePtr);
   value = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL));

   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL),
                 (value | (SCSI_CLRSHCNT+SCSI_RSTCHN)));

   while ((OSD_INEXACT8(SCSI_AICREG(SCSI_M01DFFSTAT)) & (SCSI_FIFOFREE | SCSI_DLZERO)) != (SCSI_FIFOFREE | SCSI_DLZERO))
      ;
}

/*********************************************************************
*
*  SCSIhEnableIOErr
*
*     Enable IOERR status to generate an interrupt
*
*  Return Value:  none
*
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
void SCSIhEnableIOErr (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 value;
   SCSI_UEXACT8 modeSave;

   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Set Mode 4 */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);

   value = OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE0));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE0), (value | SCSI_ENIOERR));

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
}

/*********************************************************************
*
*  SCSIhSetSeltoDelay
*
*     Set the Selection Timeout Delay in the hardware register
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 
*  Remarks:
*                 The value in the hhcb->SCSI_HF_selTimeout
*                 field is interpreted to determine the setting.
*                 One of:
*                    SCSI_256MS_SELTO_DELAY   -  256ms
*                    SCSI_128MS_SELTO_DELAY   -  128ms 
*                    SCSI_64MS_SELTO_DELAY    -   64ms
*                    SCSI_32MS_SELTO_DELAY    -   32ms 
*
*                 Assumes the sequencer is paused. 
*       
*********************************************************************/
void SCSIhSetSeltoDelay (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 modeSave;
   SCSI_UEXACT8 sxfrctl1;

   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

   sxfrctl1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));

   /* Zero the SELTO bits. */
   sxfrctl1 &= ~(SCSI_STIMESEL1 + SCSI_STIMESEL0);
#if (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE)
   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      /* Harpoon2 Rev A has a problem wherein the selection time-out time 
       * becomes twice the value that is programmed in the SXFRCTL1
       * register.  The workaround is to reduce this programmed value by
       * 1 to get the desired time-out on the bus. The details of this
       * hardware problem can be found in the Razor database entry #486.
       * This means that value SCSI_SELTO_32 is mapped onto
       * 64ms for now as only 3 values are available.
       * i.e. ALTSIM   STIMSEL1    STIMESEL0        Delay
       *         0        0            0            512ms
       *         0        0            1            256ms
       *         0        1            0            128ms
       *         0        1            1             64ms
       */
      if (hhcb->SCSI_HF_selTimeout == SCSI_32MS_SELTO_DELAY)
      {
         sxfrctl1 |= ((hhcb->SCSI_HF_selTimeout << 3) &
                      (SCSI_STIMESEL1 + SCSI_STIMESEL0));
      }
      else
      {
         /* Add one to the value to get the required delay. */ 
         sxfrctl1 |= (((hhcb->SCSI_HF_selTimeout + 1) << 3) &
                      (SCSI_STIMESEL1 + SCSI_STIMESEL0));
      }
   }
   else
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE) */
   {
      sxfrctl1 |= ((hhcb->SCSI_HF_selTimeout << 3) &
                   (SCSI_STIMESEL1 + SCSI_STIMESEL0));
   }
  
   /* Update register. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1),sxfrctl1);

   /* Restore Mode. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

}

/*********************************************************************
*
*  SCSIhResetCCControl
*
*     Reset Command Channel SCB Control and S/G Control registers
*
*  Return Value:  None
*
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
void SCSIhResetCCControl (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 modeSave;

   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSCBCTL),(SCSI_UEXACT8) 0);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSGCTL),(SCSI_UEXACT8) 0);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE1);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CCSGCTL),(SCSI_UEXACT8) 0);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
}

/*********************************************************************
*
*  SCSIhInitCCHostAddress
*
*     Initialize high 4-byte of Command Channel Host Address
*
*  Return Value:  None
*
*  Parameters:    hhcb
*
*  Remarks:
*                  
*********************************************************************/
void SCSIhInitCCHostAddress (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 modeSave;

   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SGHADDR4),(SCSI_UEXACT8) 0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SGHADDR5),(SCSI_UEXACT8) 0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SGHADDR6),(SCSI_UEXACT8) 0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SGHADDR7),(SCSI_UEXACT8) 0);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE1);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SGHADDR4),(SCSI_UEXACT8) 0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SGHADDR5),(SCSI_UEXACT8) 0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SGHADDR6),(SCSI_UEXACT8) 0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SGHADDR7),(SCSI_UEXACT8) 0);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBHADDR4),(SCSI_UEXACT8) 0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBHADDR5),(SCSI_UEXACT8) 0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBHADDR6),(SCSI_UEXACT8) 0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBHADDR7),(SCSI_UEXACT8) 0);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
}

/*********************************************************************
*
*  SCSIhGetSpeed
*
*     Calculates the Speed in tenths of Mtransfers/second.
*
*  Return Value:  Speed.
*                  
*  Parameters:    scsiRate - format of SCSIRATE register
*
*  Remarks:       For example a value of 100 returned means 10 
*                 Mtransfers/second.
*                 
*********************************************************************/
#if (SCSI_PROFILE_INFORMATION || SCSI_NEGOTIATION_PER_IOB)
SCSI_UEXACT16 SCSIhGetSpeed (SCSI_UEXACT8 SCSI_LPTR scsiRate,
                             SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_UEXACT16 speed = 50;

#if !SCSI_ASYNC_NARROW_MODE   
#if SCSI_PPR_ENABLE
   if (scsiRate[SCSI_XFER_PTCL_OPT] & SCSI_DUALEDGE)
   {
      speed = 100;
      switch (scsiRate[SCSI_XFER_PERIOD])
      {
         case 0x08:
            speed = 1600;     /* 320MB/s speed */
            break;
         case 0x09:
            speed = 800;
            break;
         case 0x0A:
            speed = 400;
            break;
         case 0x0B:
            speed = 267;
            break;
         case 0x0C:
            speed = 200;
            break;
         default:
            /* Need to work on xfer period factor: 0x0D-0x18 and 0x19 */
            /* Assume 10MB/s at this moment */
            speed = 100;
            break;
      }
   }
   else
   {
      switch(scsiRate[SCSI_XFER_PERIOD])
      {
         case 0x0A:
            speed = 400;
            break;
         case 0x0B:
            speed = 267;
            break;
         case 0x0C:
            speed = 200;
            break;
         default:
            /* Need to work on xfer period factor: */
            /* 0x0D-0x18, 0x19-0x31 and 0x32 */
            /* Assume 5MB/s at this moment */
            speed = 50;
            break;
      }
   }
#else /* !SCSI_PPR_ENABLE */
   switch(scsiRate[SCSI_XFER_PERIOD])
   {
      case 0x0A:
         speed = 400;
         break;
      case 0x0B:
         speed = 267;
         break;
      case 0x0C:
         speed = 200;
         break;
      default:
         /* Need to work on xfer period factor: */
         /* 0x0D-0x18, 0x19-0x31 and 0x32 */
         /* Assume 5MB/s at this moment */
         speed = 50;
         break;
   }
#endif /* SCSI_PPR_ENABLE */
#endif /* !SCSI_ASYNC_NARROW_MODE */
   return(speed);
}
#endif /* (SCSI_PROFILE_INFORMATION || SCSI_NEGOTIATION_PER_IOB) */

/*********************************************************************
*
*  SCSIhCalcScsiOption
*
*     Calculates the scsi synch negotiation option in the form of the
*     SCSIPERIOD, SCSIOFFSET, PROTOCOL OPTIONS, and WIDE.
*
*  Return Value:  None
*
*  Parameters:    speed            - Speed in tenths of Mtransfers/sec
*                 offset           - SCSI offset
*                 width            - Number of bits in data bus
*                 protocol options - protocol options (DT,IU,QAS) in
*                                    SCSI_XFER_PTCL_OPT format.
*                 xferRate         - xferRate to be filled
*
*  Remarks:       None.
*
*********************************************************************/
#if (SCSI_PROFILE_INFORMATION || SCSI_NEGOTIATION_PER_IOB)
void SCSIhCalcScsiOption (SCSI_UEXACT16 speed,
                          SCSI_UEXACT8 offset,
                          SCSI_UEXACT8 width,
                          SCSI_UEXACT8 ptclOptions,
                          SCSI_UEXACT8 SCSI_LPTR xferRate)
{
#if !SCSI_ASYNC_NARROW_MODE
   SCSI_UEXACT8 scsiPeriod = SCSI_ASYNC_XFER_PERIOD;

#if SCSI_PPR_ENABLE
   /* Calculate Period */
   if (ptclOptions & SCSI_DUALEDGE)
   {
      /* DT Speeds */
      if (speed >= 1600)
      {  
         scsiPeriod = 0x08;
      }
      else
      if (speed >= 800)
      {
         scsiPeriod = 0x09;
      }
      else
      if (speed >= 400)
      {
         scsiPeriod = 0x0A;
      }
      else
      if (speed < 400 && speed >= 267)
      {
         scsiPeriod = 0x0B;
      }
      else
      if (speed < 267 && speed >= 200)
      {
         scsiPeriod = 0x0C;
      }
      else
      if (speed < 200 && speed >= 160)
      {
         scsiPeriod = 0x0D;   /* Need to change */
      }
      else
      if (speed < 160 && speed >= 133)
      {
         scsiPeriod = 0x18;   /* Need to change */  
      }
      else
      if (speed < 133 && speed >= 100)
      {
         scsiPeriod = 0x19;
      }
   }
   else
   {
      /* ST Speeds */
      /* Calculate Period */
      if (speed >= 400)
      { 
         scsiPeriod = 0x0A;
      }
      else
      if (speed < 400 && speed >= 267)
      {
         scsiPeriod = 0x0B;
      }
      else
      if (speed < 267 && speed >= 200)
      {
         scsiPeriod = 0x0C;
      }
      else
      if (speed < 200 && speed >= 160)
      {
         scsiPeriod = 0x0D;   /* Need to change */
      }
      else
      if (speed < 160 && speed >= 133)
      {
         scsiPeriod = 0x18;   /* Need to change */  
      }
      else
      if (speed < 133 && speed >= 100)
      {
         scsiPeriod = 0x19;
      }
      else
      if (speed < 100 && speed >= 80)
      {
         scsiPeriod = 0x22;   /* Need to change */
      }
      else
      if (speed < 80 && speed >= 67)
      {
         scsiPeriod = 0x26;   /* Need to change */
      }
      else
      if (speed < 67 && speed >= 57)
      {
         scsiPeriod = 0x31;   /* Need to change */
      }
      else
      if (speed < 57 && speed >= 50)
      {
         scsiPeriod = 0x32;
      }
   }
#else /* !SCSI_PPR_ENABLE */
   /* Calculate Period */
   if (speed >= 400)
   {
      scsiPeriod = 0x0A;
   }
   else
   if (speed < 400 && speed >= 267)
   {
      scsiPeriod = 0x0B;
   }
   else
   if (speed < 267 && speed >= 200)
   {
      scsiPeriod = 0x0C;
   }
   else
   if (speed < 200 && speed >= 160)
   {
      scsiPeriod = 0x0D;   /* Need to change */
   } 
   else
   if (speed < 160 && speed >= 133)
   {
      scsiPeriod = 0x18;   /* Need to change */  
   }
   else
   if (speed < 133 && speed >= 100)
   {
      scsiPeriod = 0x19;
   }
   else
   if (speed < 100 && speed >= 80)
   {
      scsiPeriod = 0x22;   /* Need to change */
   }
   else
   if (speed < 80 && speed >= 67)
   {
      scsiPeriod = 0x26;   /* Need to change */
   }
   else
   if (speed < 67 && speed >= 57)
   {
      scsiPeriod = 0x31;   /* Need to change */
   }
   else
   if (speed < 57 && speed >= 50)
   {
      scsiPeriod = 0x32;
   }
#endif /* SCSI_PPR_ENABLE */

   xferRate[SCSI_XFER_PERIOD] = scsiPeriod;
   xferRate[SCSI_XFER_OFFSET] = offset;
   xferRate[SCSI_XFER_PTCL_OPT] = ptclOptions;
   if (width == 16)
   {
      xferRate[SCSI_XFER_MISC_OPT] |= SCSI_WIDE;
   }
   else
   {
      xferRate[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
   }

#else /* SCSI_ASYNC_NARROW_MODE */

   xferRate[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
   xferRate[SCSI_XFER_OFFSET] = 0x00;
   xferRate[SCSI_XFER_PTCL_OPT] = 0x00;
   xferRate[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;

#endif /* !SCSI_ASYNC_NARROW_MODE */
}
#endif /* (SCSI_PROFILE_INFORMATION || SCSI_NEGOTIATION_PER_IOB) */

/*********************************************************************
*
*  SCSIhGetProfileParameters
*
*     Gets the profile parameters from the hardware layer parameters.
*
*  Return Value:  None
*
*  Parameters:    hwInfo - pointer to hw information structure
*                 hhcb   - pointer to hhcb structure
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
void SCSIhGetProfileParameters (SCSI_HW_INFORMATION SCSI_LPTR hwInfo, 
                                SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_UEXACT8 width;
   SCSI_UEXACT8 i;
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
   SCSI_UEXACT8 enmultid;
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */

   if (hhcb->SCSI_HP_sblkctl & SCSI_SELWIDE)
   {
      width = 16;
   }
   else
   {
      width = 8;
   }

   /* Set the minimum speed for synchronous xfer at ST mode */
   hwInfo->minimumSyncSpeed = 50;

#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
   /* Initialize enmultid indicator. */
   enmultid = 0;

   if (SCSI_hHARPOON_REV_1B(hhcb) &&
       hhcb->SCSI_HF_targetMode &&
       (!hhcb->SCSI_HF_initiatorMode) &&
       hhcb->SCSI_HP_multiIdEnable)
   {
      /* Set enmultid indicator. */
      enmultid = 1;
   }
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
   
   /* Gather all the xfer option profile parameters (Current, Default, and  */
   /* Max) for all the targets, in the hw information structure.            */
   for (i = 0; i < SCSI_MAXDEV; i++)
   {
      deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];
      
      /* Get State of PCOMP register values */
      
      if (deviceTable->annexdatPerDevice0 & SCSI_PCOMP)
      {
         hwInfo->targetInfo[i].SCSI_TF_adapterPreCompEn = 1;
#if SCSI_ELEC_PROFILE
         /* If Precomp not enabled by negotiation */
         /* Do not over right default precomp setting */
#if !SCSI_FORCE_PRECOMP 
         if (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] & SCSI_PCOMP_EN)
#endif /* !SCSI_FORCE_PRECOMP */
         {
            deviceTable->SCSI_DEP_precomp  =
               (deviceTable->annexdatPerDevice0 & SCSI_PCOMP);
         }
#endif /* SCSI_ELEC_PROFILE */
      }
      else
      {   
         hwInfo->targetInfo[i].SCSI_TF_adapterPreCompEn = 0;
      }   

#if SCSI_ELEC_PROFILE
#if SCSI_STANDARD_U320_MODE
      if (SCSI_hHARPOON_REV_A(hhcb))
      {
         if (deviceTable->annexdatPerDevice0 & SCSI_SLEWRATE)
         {
           deviceTable->SCSI_DEP_slewrate  = SCSI_SLEWRATE_FAST;
         }
         else
         {
             deviceTable->SCSI_DEP_slewrate = SCSI_SLEWRATE_SLOW;
         }
         hwInfo->targetInfo[i].SCSIMaxSlewRate = SCSI_SLEWRATE_FAST;

         /* Harpoon Rev A Does not support Amplitude Control */
         hwInfo->targetInfo[i].SCSIMaxAmplitudeLevel = 0;

         deviceTable->SCSI_DEP_amplitude = 0;
      }
      else
#endif /* SCSI_STANDARD_U320_MODE */
      {
         /* Set Max slewrate and amplitude values */
         hwInfo->targetInfo[i].SCSIMaxSlewRate = SCSI_MAX_HARPOON2B_SLEWLEVEL;
         hwInfo->targetInfo[i].SCSIMaxAmplitudeLevel = SCSI_MAX_HARPOON2B_AMPLITUDE;

#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
         if (enmultid)
         {
            /* Slewrate is in per-device control 3 when ENMULTI is enabled */
            /* Harpoon Rev B has 4 bits of slew rate */
            deviceTable->SCSI_DEP_slewrate  =
               (deviceTable->annexdatPerDevice3 & SCSI_MTID_SLEWRATE);

            /* Amplitude is in per-device control 2 */
            /* Harpoon Rev B has 3 bits of amplitude Control */
            deviceTable->SCSI_DEP_amplitude  =
                   (deviceTable->annexdatPerDevice2 & SCSI_AMPLITUDE);
         }
         else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
         {
            /* Slewrate is in per-device byte 0 */
            /* Harpoon Rev B has 4 bits of slew rate */
            deviceTable->SCSI_DEP_slewrate =
               (deviceTable->annexdatPerDevice0 >> 3);

            /* Amplitude is in per-device byte 2 */
            /* Harpoon Rev B has 3 bits of amplitude Control */
            deviceTable->SCSI_DEP_amplitude =
               (deviceTable->annexdatPerDevice2 & SCSI_AMPLITUDE);
         }
      }
#endif /* SCSI_ELEC_PROFILE */

      hwInfo->targetInfo[i].SCSIDefaultProtocolOptionMask =    
      SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT];
           
      if (deviceTable->SCSI_DF_ptlOptMaskEnable)
      {
         hwInfo->targetInfo[i].SCSI_TF_ptclOptMaskEnable = 1;
      }
      else
      {
         hwInfo->targetInfo[i].SCSI_TF_ptclOptMaskEnable = 0;
         /* Keep field updated for flow  control options. */
         hwInfo->targetInfo[i].SCSIDefaultProtocolOptionMask |=    
            SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &
                    ~((SCSI_UEXACT8)(SCSI_DUALEDGE | SCSI_QUICKARB | SCSI_PACKETIZED));
      }
                        
#if SCSI_PACKETIZED_IO_SUPPORT
      hwInfo->targetInfo[i].SCSIMaxProtocolOptionMask =    
           SCSI_MAXPROTOCOL_MASK_DEFAULT;

      if (SCSI_hHARPOON_REV_B(hhcb) || SCSI_hDCH_SCSI_CORE(hhcb))
      {
         hwInfo->targetInfo[i].SCSIMaxProtocolOptionMask |=  SCSI_RTI;
      }

#if SCSI_TARGET_OPERATION
      if (hhcb->SCSI_HF_targetMode && (!hhcb->SCSI_HF_initiatorMode))
      {
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
         if (SCSI_hHARPOON_REV_1B(hhcb))
         {
            /* Supports packetized but not SCSI_WR_FLOW or SCSI_RD_STRM 
             * when target mode only.
             */
            hwInfo->targetInfo[i].SCSIMaxProtocolOptionMask &= 
                  (~(SCSI_WR_FLOW | SCSI_RD_STRM | SCSI_HOLD_MCS));
         }
         else
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
         {
            /* Does not support any packetized features when target mode
             * only. (I.e. Only DT and QAS are supported.)
             */
            hwInfo->targetInfo[i].SCSIMaxProtocolOptionMask &= 
               (SCSI_QAS_REQ | SCSI_DT_REQ);
         }         /* Target mode does not support IU related features. */
      }
#endif /* SCSI_TARGET_OPERATION */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

      /* Retrieve default protocol option */
      /* Dual-Edge transfer mode */
      if (SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] & SCSI_DUALEDGE)
      {
         hwInfo->targetInfo[i].SCSI_TF_dtcEnable = 1;
#if SCSI_PACKETIZED_IO_SUPPORT

         if (SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &
             SCSI_PACKETIZED)
         { 
            if (SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &
                SCSI_QUICKARB)
            {
               hwInfo->targetInfo[i].SCSIDefaultProtocolOption =
                  SCSI_DT_DATA_WITH_IU_AND_QAS;
            }
            else
            {
               hwInfo->targetInfo[i].SCSIDefaultProtocolOption =
                  SCSI_DT_DATA_WITH_IU;
            }
         }   
         else
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
         if (SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &
             SCSI_QUICKARB)
         {
            hwInfo->targetInfo[i].SCSIDefaultProtocolOption =
               SCSI_DT_DATA_WITH_CRC_AND_QAS;
         }
         else
         {
            hwInfo->targetInfo[i].SCSIDefaultProtocolOption =
               SCSI_DT_DATA_WITH_CRC;
         }
      }
      else
      {
         hwInfo->targetInfo[i].SCSI_TF_dtcEnable = 0;
         hwInfo->targetInfo[i].SCSIDefaultProtocolOption = SCSI_ST_DATA;
      }

      /* Retrieve current protocol option */
      
      /* Keep field updated even if not used. */
      hwInfo->targetInfo[i].SCSICurrentProtocolOptionMask =    
           SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT];
      
      /* Dual-Edge transfer mode */
      if (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] & SCSI_DUALEDGE)
      {
#if SCSI_PACKETIZED_IO_SUPPORT
         if (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &
             SCSI_PACKETIZED)
         { 
            if (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &
                SCSI_QUICKARB)
            {
               hwInfo->targetInfo[i].SCSICurrentProtocolOption =
                  SCSI_DT_DATA_WITH_IU_AND_QAS;
            }
            else
            {
               hwInfo->targetInfo[i].SCSICurrentProtocolOption =
                  SCSI_DT_DATA_WITH_IU;
            }
         }   
         else
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
         if (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &
             SCSI_QUICKARB)
         {
            hwInfo->targetInfo[i].SCSICurrentProtocolOption =
               SCSI_DT_DATA_WITH_CRC_AND_QAS;
         }
         else
         {
            hwInfo->targetInfo[i].SCSICurrentProtocolOption =
               SCSI_DT_DATA_WITH_CRC;
         }
      }
      else
      {
         hwInfo->targetInfo[i].SCSICurrentProtocolOption = SCSI_ST_DATA;
      }

      hwInfo->targetInfo[i].SCSIMaxSpeed =
         SCSIhGetSpeed((SCSI_MAX_XFER(deviceTable)), hhcb);

      hwInfo->targetInfo[i].SCSIDefaultSpeed =
               SCSIhGetSpeed((SCSI_DEFAULT_XFER(deviceTable)), hhcb);

      hwInfo->targetInfo[i].SCSICurrentSpeed =
               SCSIhGetSpeed((SCSI_CURRENT_XFER(deviceTable)), hhcb);
        
      hwInfo->targetInfo[i].SCSICurrentOffset =
               SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_OFFSET];
         
      hwInfo->targetInfo[i].SCSIDefaultOffset =
               SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_OFFSET];

      hwInfo->targetInfo[i].SCSIMaxOffset = SCSI_MAX_OFFSET;

      hwInfo->targetInfo[i].SCSIMaxWidth = width;
   
      if (SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_MISC_OPT] & SCSI_WIDE)
      {
         hwInfo->targetInfo[i].SCSIDefaultWidth = 16;
      }
      else
      {
         hwInfo->targetInfo[i].SCSIDefaultWidth = 8;
      }
   
      if (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_MISC_OPT] & SCSI_WIDE)
      {
         hwInfo->targetInfo[i].SCSICurrentWidth = 16;
      }
      else
      {
         hwInfo->targetInfo[i].SCSICurrentWidth = 8;
      }

      hwInfo->targetInfo[i].SCSI_TF_scsiOptChanged = 0;
   }

   /* Determine the transceiver mode */
   hwInfo->SCSI_PF_transceiverMode = SCSI_UNKNOWN_MODE;

   if (hhcb->SCSI_HP_sblkctl & SCSI_XCVR)
   {
      if ((hhcb->SCSI_HP_sblkctl & SCSI_ENAB20) &&
          (!(hhcb->SCSI_HP_sblkctl & SCSI_ENAB40)))
      {
         /* HVD mode */ 
         hwInfo->SCSI_PF_transceiverMode = SCSI_HVD_MODE;
      }
   }   
   else
   {
      if ((hhcb->SCSI_HP_sblkctl & SCSI_ENAB40) &&
          (!(hhcb->SCSI_HP_sblkctl & SCSI_ENAB20)))
      {
         /* LVD mode */
         hwInfo->SCSI_PF_transceiverMode = SCSI_LVD_MODE;
      }
      else if ((!(hhcb->SCSI_HP_sblkctl & SCSI_ENAB40)) &&
               (hhcb->SCSI_HP_sblkctl & SCSI_ENAB20))
      {
         /* SE mode */
         hwInfo->SCSI_PF_transceiverMode = SCSI_SE_MODE;
      } 
   } 
}
#endif /* SCSI_PROFILE_INFORMATION */

/*********************************************************************
*
*  SCSIhPutProfileParameters
*
*     Incorporates the changes in profile parameters into the hardware
*     layer parameters.
*
*  Return Value:  None
*
*  Parameters:    hwInfo - pointer to hw information structure
*                 hhcb   - pointer to hhcb structure
*
*  Remarks:
*
*********************************************************************/
#if SCSI_PROFILE_INFORMATION  
void SCSIhPutProfileParameters (SCSI_HW_INFORMATION SCSI_LPTR hwInfo, 
                                SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_UEXACT8 offset;
   SCSI_UEXACT8 i;
   SCSI_UEXACT8 protocolOptions;
   
   /* For all the targets, incorporate the xfer option paramters from the  */
   /* hw information structure.                                            */
   for (i = 0; i < SCSI_MAXDEV; i++)
   {
      deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];

#if SCSI_ELEC_PROFILE
      if (hwInfo->targetInfo[i].SCSI_TF_elecOptChanged)
      {
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
         if (SCSI_hHARPOON_REV_1B(hhcb) &&
             hhcb->SCSI_HF_targetMode &&
             (!hhcb->SCSI_HF_initiatorMode))
         {
            /* Set the upper 4 bits of NEGODAT to our SCSI ID for 
             * multiple target ID support.
             */
            /* Offset to location in auto-rate-offset table by specified target ID */
            /* Reset slew rate / precomp / amp to defaults */
            SCSI_hSETELECINFORMATION(hhcb,
                                     (i | (hhcb->hostScsiID << 4)),
                                     SCSI_CURRENT_XFER(&SCSI_DEVICE_TABLE(hhcb)[i]));
         }  
         else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
         {
            SCSI_hSETELECINFORMATION(hhcb,i,SCSI_CURRENT_XFER(deviceTable));
         }
      }
#endif /* SCSI_ELEC_PROFILE */
      if (hwInfo->targetInfo[i].SCSI_TF_scsiOptChanged)
      {

         /* Make TARGET CAP entry to be current negoXferIndex */
         deviceTable->negoXferIndex = SCSI_TARGET_CAP_ENTRY;

         /* Make TARGET PROFILE entry the defaultXferIndex. */
         deviceTable->defaultXferIndex = SCSI_TARGET_PROFILE_ENTRY;

#if SCSI_DOMAIN_VALIDATION
         /* Initialize preDVXferIndex to defaultXferIndex. */
         deviceTable->preDVXferIndex = deviceTable->defaultXferIndex;
#endif /* SCSI_DOMAIN_VALIDATION */
         
         offset = hwInfo->targetInfo[i].SCSIDefaultOffset;

         if (offset > SCSI_MAX_OFFSET)
         {
            offset = SCSI_MAX_OFFSET;
         }

         if (hwInfo->targetInfo[i].SCSI_TF_ptclOptMaskEnable)
         {
            protocolOptions = (SCSI_UEXACT8)hwInfo->targetInfo[i].SCSIDefaultProtocolOptionMask;
         }
         else
         { 
            /* Translate protocol options */
            switch (hwInfo->targetInfo[i].SCSIDefaultProtocolOption)
            {
               case SCSI_ST_DATA:
                  protocolOptions = 0;
                  break;

               case SCSI_DT_DATA_WITH_CRC:
                  protocolOptions = (SCSI_UEXACT8)SCSI_DUALEDGE;
                  break;

               case SCSI_DT_DATA_WITH_IU:
                  protocolOptions =
                     (SCSI_UEXACT8)(SCSI_DUALEDGE | SCSI_PACKETIZED);
                  break;
               
               case SCSI_DT_DATA_WITH_CRC_AND_QAS:
                  protocolOptions =
                     (SCSI_UEXACT8)(SCSI_DUALEDGE | SCSI_QUICKARB);
                 break;

               case SCSI_DT_DATA_WITH_IU_AND_QAS:
                  protocolOptions =
                     (SCSI_UEXACT8)(SCSI_DUALEDGE | SCSI_QUICKARB | SCSI_PACKETIZED);
                  break;

               default:
            
                  /* Should never get here */
                  OSDDebugPrint(1,
                             ("SCSIhPutProfileParameters Invalid Protocol %x ",
                              hwInfo->targetInfo[i].SCSIDefaultProtocolOption));
                     
                  protocolOptions = 0;
                  break;
            }
            
            if (protocolOptions & SCSI_PACKETIZED)
            { 
                /* If not in mask mode still use optionMask values for previously
                   unsupported PPR bits to allow for request of HBA defaults.*/
               protocolOptions |= 
                   (SCSI_UEXACT8)hwInfo->targetInfo[i].SCSIDefaultProtocolOptionMask
                   & ~((SCSI_UEXACT8)(SCSI_DUALEDGE | SCSI_QUICKARB | SCSI_PACKETIZED));
            }
            
         } /* optmask enable */

         SCSIhCalcScsiOption(hwInfo->targetInfo[i].SCSIDefaultSpeed,
                             offset,
                             hwInfo->targetInfo[i].SCSIDefaultWidth,
                             protocolOptions,
                             SCSI_TARGET_PROFILE_XFER(deviceTable));

#if SCSI_ELEC_PROFILE
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
                                     (i | (hhcb->hostScsiID << 4)),
                                     SCSI_CURRENT_XFER(deviceTable));
         }
         else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
         {
            SCSI_hSETELECINFORMATION(hhcb,i,SCSI_CURRENT_XFER(deviceTable));
         }
#endif /* SCSI_ELEC_PROFILE */

#if SCSI_NEGOTIATION_PER_IOB
         if (offset)
         {
            deviceTable->origOffset = offset;
         }
#endif /* SCSI_NEGOTIATION_PER_IOB */

#if !SCSI_NEGOTIATION_PER_IOB
         
         /* Force negotiation if device is idle */
         if (SCSIHCheckDeviceIdle(hhcb,i))
         {
            /* turn on the negotiation initiation */
            if (SCSI_hWIDEORSYNCNEGONEEDED(deviceTable))
            {
#if SCSI_PACKETIZED_IO_SUPPORT
               /* Setup hardware to re-negotiate in packetized mode */
               SCSIHSetupNegoInPacketized(hhcb,i);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

               deviceTable->SCSI_DF_needNego = 1;
            }
            else
            {
               deviceTable->SCSI_DF_needNego = 0;
            }
         }
#endif /* !SCSI_NEGOTIATION_PER_IOB */
      }
   }
}
#endif /* SCSI_PROFILE_INFORMATION */

/*********************************************************************
*
*  SCSIhGetArpNewSCBQOffset
*
*     Gets the ARP New SCB Queue Offset, SNSCB_QOFF, register value.
*
*  Return Value:  None
*
*  Parameters:    hhcb - pointer to hhcb structure
*
*  Remarks:
*
*********************************************************************/
#if (SCSI_STANDARD_MODE || SCSI_SAVE_RESTORE_STATE)
SCSI_UEXACT16 SCSIhGetArpNewSCBQOffset (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT16 snscbqoff;
   SCSI_UEXACT8 modeSave;

   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE2);

   /* Read the Sequencer New Queue Offset register.                          */
   /* NOTE : The upper byte of the register should be read first because if  */
   /* the register had the value of, say, 0x4FF and we read the low byte     */
   /* first (which is 0xFF), the hardware automatically increments the value */
   /* to 0x500 before we could read the upper byte.  So, the value we would  */
   /* get when we read the upper byte would be 0x5, resulting in a value of  */
   /* 0x5FF, instead of the correct value of 0x4FF.  */
   snscbqoff =
      (((SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF+1))) << 8);
   snscbqoff += (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF));

   /* As the SNSCB_QOFF register auto increments on a read, we need to */
   /* write back what we read (this will adjust the NEW_SCB_AVAIL bit  */
   /* in the QOFF_CTLSTA register automatically, if needed).           */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF),(SCSI_UEXACT8)snscbqoff);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SNSCB_QOFF+1),(SCSI_UEXACT8)(snscbqoff >> 8));

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   return(snscbqoff);
}
#endif /* (SCSI_STANDARD_MODE || SCSI_SAVE_RESTORE_STATE) */

/*********************************************************************
*
*  SCSIhGetTimerCount
*
*     Converts microseconds value to a timer count units.
*     The result is rounded UP to the nearest multiple of
*     timer resolution (SCSI_U320_TIMER_GRANULARITY).
*
*  Return Value:  a value that can be used to program the S/W timer 
*                 in the U320 ASIC.
*               
*  Parameters:    hhcb  - pointer to hhcb structure
*                 usecs - a value in microseconds
*
*  Remarks:
*
*********************************************************************/
SCSI_UEXACT32 SCSIhGetTimerCount (SCSI_HHCB SCSI_HPTR hhcb,
                                  SCSI_UEXACT32 usecs)
{
   SCSI_UEXACT32 timerCount;
   
   timerCount = (usecs+SCSI_U320_TIMER_GRANULARITY-1) /
                   SCSI_U320_TIMER_GRANULARITY;
   
   return (timerCount);
}

/*********************************************************************
*
*  SCSIhSetModeToCurrentFifo
*
*     Set mode pointer register to the current fifo
*
*  Return Value:  None
*               
*  Parameters:    hhcb - pointer to hhcb structure
*
*  Remarks:       Assumption we are in mode 3.
*
*********************************************************************/
void SCSIhSetModeToCurrentFifo (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_DFFSTAT)) & SCSI_CURRFIFO)
   {
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE1);
   }
   else
   {
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE0);
   }
}

/*********************************************************************
*
*  SCSIhGetHIOBFromCurrentFifo
*
*     Get the active HIOB from the current fifo.
*
*  Return Value:  hiob
*                  
*  Parameters:    hhcb
*
*  Remarks:
*
*********************************************************************/
SCSI_HIOB SCSI_IPTR SCSIhGetHIOBFromCurrentFifo (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_UEXACT16 scbNumber;
   SCSI_UEXACT8 savedMode;

   /* Save the mode register */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   /* Set mode pointer register to the current fifo to get active SCB */
   SCSIhSetModeToCurrentFifo(hhcb);
   scbNumber = ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb)+1))) << 8) |
                (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb))));

   /* Get active HIOB */
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

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);

   return(hiob);
}

/*********************************************************************
*
*  SCSIhStandardSetBreakPoint
*
*     Set breakpoint at the given entry point.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 entryBitMap
*
*  Remarks:
*                  
*********************************************************************/
#if (SCSI_STANDARD_MODE && SCSI_SEQBREAKPOINT_WORKAROUND)
void SCSIhStandardSetBreakPoint (SCSI_HHCB SCSI_HPTR hhcb,
                                 SCSI_UEXACT8 entryBitMap)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 brkCnt;

   /* Save the HCNTRL value */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* Pause the chip if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   if (entryBitMap & SCSI_ENTRY_IDLE_LOOP0_BREAK)
   {
      brkCnt = OSD_INEXACT8(SCSI_AICREG(SCSI_hENT_PT_BITMAP(hhcb)));
      brkCnt |= (SCSI_UEXACT8)SCSI_ENTRY_IDLE_LOOP0_BREAK;
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_hENT_PT_BITMAP(hhcb)),brkCnt);
   }
   else if (entryBitMap & SCSI_ENTRY_EXPANDER_BREAK)
   {
      brkCnt = OSD_INEXACT8(SCSI_AICREG(SCSI_hENT_PT_BITMAP(hhcb)));
      brkCnt |= (SCSI_UEXACT8)SCSI_ENTRY_EXPANDER_BREAK;
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_hENT_PT_BITMAP(hhcb)),brkCnt);
   }

   /* Restore HCNTRL if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
}
#endif /* (SCSI_STANDARD_MODE && SCSI_SEQBREAKPOINT_WORKAROUND) */

/*********************************************************************
*
*  SCSIhStandardClearBreakPoint
*
*     Clear breakpoint at the given entry point.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 entryBitMap
*
*  Remarks:
*                  
*********************************************************************/
#if (SCSI_STANDARD_MODE && SCSI_SEQBREAKPOINT_WORKAROUND)
void SCSIhStandardClearBreakPoint (SCSI_HHCB SCSI_HPTR hhcb,
                                   SCSI_UEXACT8 entryBitMap)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 brkCnt;

   if (entryBitMap & SCSI_ENTRY_IDLE_LOOP0_BREAK)
   {
      brkCnt = OSD_INEXACT8(SCSI_AICREG(SCSI_hENT_PT_BITMAP(hhcb)));
      brkCnt &= ~(SCSI_UEXACT8)SCSI_ENTRY_IDLE_LOOP0_BREAK;
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_hENT_PT_BITMAP(hhcb)),brkCnt);
   }
   else if (entryBitMap & SCSI_ENTRY_EXPANDER_BREAK)
   {
      brkCnt = OSD_INEXACT8(SCSI_AICREG(SCSI_hENT_PT_BITMAP(hhcb)));
      brkCnt &= ~(SCSI_UEXACT8)SCSI_ENTRY_EXPANDER_BREAK;
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_hENT_PT_BITMAP(hhcb)),brkCnt);
   }
}
#endif /* (SCSI_STANDARD_MODE && SCSI_SEQBREAKPOINT_WORKAROUND) */

/*********************************************************************
*
*  SCSIhStandardSetBreakPoint
*
*     Set breakpoint at the given entry point.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 entryBitMap
*
*  Remarks:
*                  
*********************************************************************/
#if (SCSI_STANDARD_MODE && !SCSI_ASPI_SUPPORT_GROUP1)
void SCSIhStandardSetBreakPoint (SCSI_HHCB SCSI_HPTR hhcb,
                                 SCSI_UEXACT8 entryBitMap)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT16 expanderBreakAddr;

   /* Save the HCNTRL value */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* Pause the chip if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   if (entryBitMap & SCSI_ENTRY_IDLE_LOOP0_BREAK)
   {
      /* Program to idle loop0 entry for breakpoint */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRKADDR0), 
                    (SCSI_UEXACT8)(SCSI_hIDLE_LOOP0(hhcb) >> 2));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRKADDR1), 
                    (SCSI_UEXACT8)(SCSI_hIDLE_LOOP0(hhcb) >> 10));
   }
   else if (entryBitMap & SCSI_ENTRY_EXPANDER_BREAK)
   {
      /* Store in valriable to avoid compiler warning when value
       * is > 10 bits.
       */
      expanderBreakAddr = (SCSI_UEXACT16) SCSI_hEXPANDER_BREAK(hhcb);
      
      /* Program to expander break entry for breakpoint */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRKADDR0), 
                    (SCSI_UEXACT8)(expanderBreakAddr >> 2));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRKADDR1), 
                    (SCSI_UEXACT8)(expanderBreakAddr >> 10));
   }

   /* Enable break address interrupt */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0),
       (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_SEQCTL0)) | SCSI_BRKADRINTEN));

   /* Restore HCNTRL if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
}
#endif /* (SCSI_STANDARD_MODE  && !SCSI_ASPI_SUPPORT_GROUP1)*/

/*********************************************************************
*
*  SCSIhStandardClearBreakPoint
*
*     Clear breakpoint at the given entry point.
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 entryBitMap
*
*  Remarks:
*                  
*********************************************************************/
#if (SCSI_STANDARD_MODE && !SCSI_ASPI_SUPPORT_GROUP1)
void SCSIhStandardClearBreakPoint (SCSI_HHCB SCSI_HPTR hhcb,
                                   SCSI_UEXACT8 entryBitMap)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   
   /* Zero out breakpoint address and turn on 'disable breakpoint' bit */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRKADDR0), (SCSI_UEXACT8)0);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_BRKADDR1), (SCSI_UEXACT8)SCSI_BRKDIS);

   /* Disable break address interrupt */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SEQCTL0),
       (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_SEQCTL0)) & ~SCSI_BRKADRINTEN));
}
#endif /* (SCSI_STANDARD_MODE && !SCSI_ASPI_SUPPORT_GROUP1) */

/*********************************************************************
*
*  SCSIhStandardU320SetIntrThresholdCount
*
*     Sets the interrupt threshold in the scratch register
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 thresholdCount
*
*  Remarks:       Mode 3 is assumed
*                  
*********************************************************************/
#if (SCSI_STANDARD_U320_MODE || SCSI_STANDARD_ENHANCED_U320_MODE)
void SCSIhStandardU320SetIntrThresholdCount (SCSI_HHCB SCSI_HPTR hhcb,
                                             SCSI_UEXACT8 thresholdCount)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_INTR_THRSH_MAXCNT1),
                 thresholdCount);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_INTR_THRSH_MAXCNT2),
                 thresholdCount);
}
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_STANDARD_ENHANCED_U320_MODE) */
/*********************************************************************
*
*  SCSIhDchU320SetIntrThresholdCount
*
*     Sets the interrupt threshold in the scratch register
*
*  Return Value:  None
*
*  Parameters:    hhcb
*
*  Remarks: Mode 3 is assumed
*                  
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320SetIntrThresholdCount (SCSI_HHCB SCSI_HPTR hhcb,
                                        SCSI_UEXACT8 thresholdCount)
{

   SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_INTR_THRSH_MAXCNT1),
                 thresholdCount);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_INTR_THRSH_MAXCNT2),
                 thresholdCount);

}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardU320SetIntrFactorThreshold
*
*     Sets the interrupt factor threshold in the scratch register
*
*  Return Value:  None
*
*  Parameters:    hhcb
*                 Factorthreshold
*
*  Remarks:       Mode 3 is assumed
*                  
*********************************************************************/
#if (SCSI_STANDARD_U320_MODE || SCSI_STANDARD_ENHANCED_U320_MODE)
void SCSIhStandardU320SetIntrFactorThreshold (SCSI_HHCB SCSI_HPTR hhcb,
                                              SCSI_UEXACT8 factorThreshold)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_INTR_THRSH_TIMEOUT1),
                      factorThreshold);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_INTR_THRSH_TIMEOUT2),
                      factorThreshold);
}
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_STANDARD_ENHANCED_U320_MODE) */

/*********************************************************************
*
*  SCSIhDchU320SetIntrFactorThreshold
*
*     Sets the interrupt factor threshold in the scratch register
*
*  Return Value:  None
*
*  Parameters:    hhcb
*
*  Remarks:       Mode 3 is assumed
*                  
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320SetIntrFactorThreshold (SCSI_HHCB SCSI_HPTR hhcb,
                                              SCSI_UEXACT8 Factorthreshold)
{
   SCSI_REGISTER scsiRegister = hhcb->scsiRegister;

   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_INTR_THRSH_TIMEOUT1),
                 Factorthreshold);
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_INTR_THRSH_TIMEOUT2),
                 Factorthreshold);
}
#endif /* SCSI_DCH_U320_MODE */ 

/*********************************************************************
*
*  SCSIhStandardU320SetDisconnectDelay
*
*     Sets the disconnect delay value from the hhcb into the
*     scratch RAM location
*
*  Return Value:  None
*
*  Parameters:    hhcb
*
*  Remarks:       To be implemented.
*                  
*********************************************************************/
#if (SCSI_STANDARD_U320_MODE || SCSI_DCH_U320_MODE)
void SCSIhStandardU320SetDisconnectDelay (SCSI_HHCB SCSI_HPTR hhcb)
{
}
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_DCH_U320_MODE) */

/*********************************************************************
*
*  SCSIhCheckSigSCBFF
*
*     Check to see whether signature of SCB is valid or not
*
*  Return Value:  1 if valid
*                 0 if not valid
*                  
*  Parameters:    hhcb
*                 dataBuffer
*
*  Remarks:
*
*********************************************************************/
#if (!SCSI_BIOS_SUPPORT && SCSI_SCSISELECT_SUPPORT) 
SCSI_UINT8 SCSIhCheckSigSCBFF (SCSI_HHCB SCSI_HPTR hhcb,
                               SCSI_UEXACT8 SCSI_SPTR dataBuffer)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_INT i;
   SCSI_UEXACT16 savedScbptr;
   SCSI_UEXACT16 nvRamScbptr;
   SCSI_UINT8 returnValue;
   SCSI_UEXACT8 hcntrl;       
   SCSI_UEXACT8 savedMode;
   SCSI_UEXACT8 signature[4];
      
   /* Save the HCNTRL value. */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* Pause the chip, if necessary. */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   /* Save the MODE_PTR register. */
   savedMode = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   
   /* Switch to mode 3 - a known mode for SCBPTR access. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
   /* Save mode 3 SCBPTR value. */
   savedScbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
                 ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

   /* Load scb ptr with 0xFF */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                      (SCSI_UEXACT8)SCSI_U320_SCBFF_PTR);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                      (SCSI_UEXACT8)(SCSI_U320_SCBFF_PTR>>8));

   nvRamScbptr = (SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+SCSI_SCBFF_NVRAM_SCB_NUM_OFST)));

   /* Load scb ptr with 0xFF */
   SCSI_READ_SCB_RAM(hhcb,
                    (SCSI_UEXACT8 SCSI_SPTR) &signature[0],
                    (SCSI_UEXACT16)SCSI_U320_SCBFF_PTR, 
                    (SCSI_UEXACT8) 0,
                    (SCSI_UEXACT8) 4);

   /* if the NVRam SCB # is valid and correct Signature */
   /* (ADPT, BIOS, or ASPI), then copy data to buffer.  */
   if ((((signature[0] == 'A') && (signature[1] == 'D') &&
         (signature[2] == 'P') && (signature[3] == 'T')) ||
        ((signature[0] == 'B') && (signature[1] == 'I') &&
         (signature[2] == 'O') && (signature[3] == 'S')) ||
        ((signature[0] == 'A') && (signature[1] == 'S') &&
         (signature[2] == 'P') && (signature[3] == 'I'))) &&
       (nvRamScbptr != 0x00ff))
   {
      /* Load scb ptr with NVRAM Scb # */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8) nvRamScbptr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(nvRamScbptr>>8));

      /* copy 64 bytes from SCB ram to nvmdata */
      for (i = 0; i <= 63; i++)
      {
         *((SCSI_UEXACT8 SCSI_SPTR) (dataBuffer + i)) =
                    OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE + i)); 
      }
      returnValue = 1;
   }
   else
   {
      returnValue = 0; 
   }

   /* Restore mode 3 SCBPTR. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),(SCSI_UEXACT8)savedScbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(savedScbptr >> 8));

   /* Restore MODE_PTR register. */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,savedMode);  
   
   /* Restore HCNTRL if necessary */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }

   return(returnValue);
}
#endif /* (!SCSI_BIOS_SUPPORT && SCSI_SCSISELECT_SUPPORT) */

/*********************************************************************
*
*  SCSIhChangeXferOption
*
*     Incorporates the changes in xfer option parameters into the hardware
*     layer parameters.  Note that this routine assumes that Device for which
*     the xfer option needs to be changed is idle.
*
*  Return Value:  None
*
*  Parameters:    hiob
*
*  Remarks:       None.
*
*********************************************************************/
#if SCSI_NEGOTIATION_PER_IOB
void SCSIhChangeXferOption (SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_UNIT_CONTROL SCSI_IPTR targetUnit = SCSI_TARGET_UNIT(hiob);
   SCSI_HHCB SCSI_HPTR hhcb = targetUnit->hhcb;
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl = targetUnit->targetControl;
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_UEXACT16 speed;
   SCSI_UEXACT8 offset;
   SCSI_UEXACT8 width;
   SCSI_UEXACT16 currentXferOption;
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   
   /* For the given target, incorporate the incoming xfer option paramters. */
   /* The xfer option parameters come through HIOB. */

   deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetUnit->scsiID];

   /* Get the current xfer option parameters */
   currentXferOption = SCSI_hGETXFEROPTION(hhcb,targetUnit->scsiID);
   offset = (SCSI_UEXACT8)((currentXferOption >> 8) & SCSI_BAYONET_OFFSET);
   if (currentXferOption & SCSI_WIDE_XFER)
   {
      width = 16;
   }
   else
   {
      width = 8;
   }
   speed = SCSIhGetSpeed(deviceTable->bayScsiRate, 0);

   /* Change the width according the force bits */
   if ((hiob->SCSI_IF_forceWide) || (hiob->SCSI_IF_forceNarrow))
   {
      if (hiob->SCSI_IF_forceWide)
      {
         width = 16;
      }
      else
      {
         width = 8;
      }
   }

   /* Adjust the 'setForWide' parameter according to the resultant width */
   if (width == 16)
   {
      deviceTable->SCSI_DF_setForWide = 1;  /* Set setForWide flag */
   }
   else
   {
      deviceTable->SCSI_DF_setForWide = 0;  /* Clear setForWide flag */

      /* If the device's running in wide mode, we need to clear       */
      /* the setForWide flag and set the width to 16 so that the wide */
      /* xfer bit will be set when the new scsiOption get calucated.  */
      /* This way when the next IOB for this target start execute,    */
      /* CHIM will intiate wide negotiation. By then, it will clear   */
      /* the wide xfer bit because of the setForWide was clear.       */
      if (currentXferOption & SCSI_WIDEXFER)
      {
         width = 16;
      }
   }

   /* Do nothing if the speed and offset are the same */
   if ((hiob->SCSI_IF_forceSync) || (hiob->SCSI_IF_forceAsync))
   {
      if ((hiob->SCSI_IF_forceSync))
      {
         offset = deviceTable->origOffset; 
      }
      else
      {
         offset = 0;
      }
   }

   /* If user wants a non-zero offset and speed above async range, */
   /* this means do sync neg. with hardware default offset */
   if (offset)
   {
      deviceTable->SCSI_DF_setForSync = 1;   /* Set setForSync flag */
   }
   else
   {
      deviceTable->SCSI_DF_setForSync = 0;   /* Clear setForSync flag */
      /* Since the default offset is zero and/or speed is less than   */
      /* 36, which means do async. xfer, we need to clear the         */
      /* setForSync flag and set the offset to the current offset.    */
      /* This way when the next IOB for this target start execute,    */
      /* CHIM will intiate sync. negotiation if the current offset is */
      /* nonzero. By then, it will set the offset to zero because of  */
      /* the setForSync was clear.                                    */
      offset = (SCSI_UEXACT8)((currentXferOption >> 8) & SCSI_BAYONET_OFFSET);
   }

   /* Incorporate the xfer option changes into the deviceTable and kick start */
   /* the negotiation. */
   if(speed >= 800)
   {
      currentXferOption = SCSIhCalcScsiOption(speed,offset,width, 1, hhcb);
   }
   else
   {
      currentXferOption = SCSIhCalcScsiOption(speed,offset,width, 0, hhcb);
   }
   deviceTable->bayScsiRate = (SCSI_UEXACT8)(currentXferOption);
   deviceTable->bayScsiOffset = (SCSI_UEXACT8)(currentXferOption >> 8);

   /* Save 'forceNego' flags into the deviceTable */
   deviceTable->SCSI_DNF_forceSync = hiob->SCSI_IF_forceSync;
   deviceTable->SCSI_DNF_forceAsync = hiob->SCSI_IF_forceAsync;
   deviceTable->SCSI_DNF_forceWide = hiob->SCSI_IF_forceWide;
   deviceTable->SCSI_DNF_forceNarrow = hiob->SCSI_IF_forceNarrow;

   /* Clear scsiOption before assigning new value to it */
   deviceTable->scsiOption = 0;

   /* Reassign scsiOption */
   SCSIhChangeNegotiation(targetUnit);
}
#endif /* SCSI_NEGOTIATION_PER_IOB */

/*******************************************************************************
*
*  SCSIhChangeNegotiation
*
*     Changes scsiOption field to reflect the negotiation changes from the
*     deviceTable 'negoFlags' fields.
*
*  Return Value:  None
*
*  Parameters:    targetUnit - pointer to target unit structure
*
*  Remarks:       None.
*
*******************************************************************************/
#if SCSI_NEGOTIATION_PER_IOB
void SCSIhChangeNegotiation (SCSI_UNIT_CONTROL SCSI_UPTR targetUnit)
{
   SCSI_HHCB SCSI_HPTR hhcb = targetUnit->hhcb;
   SCSI_DEVICE SCSI_DPTR deviceTable;
   register SCSI_REGISTER scsiRegister = targetUnit->hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;

   deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetUnit->scsiID];

   /* For Harpoon, the chip should be paused before accessing  */
   /* scratch RAM or SCB array                                 */
   if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   /* Turn on sync negotiation */
   if ((deviceTable->SCSI_DNF_forceSync) || (deviceTable->SCSI_DNF_forceAsync))
   {
#if SCSI_BIOS_SUPPORT
      if (!hhcb->SCSI_HF_biosRunTimeEnabled)
#endif /* SCSI_BIOS_SUPPORT */
      {
         deviceTable->scsiOption |= SCSI_SYNC_XFER;
      }   
   }

   /* Turn on wide/narrow negotiation if the adapter supports wide bus */
   if (OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL)) & SCSI_SELWIDE)
   {
      if ((deviceTable->SCSI_DNF_forceWide) ||
          (deviceTable->SCSI_DNF_forceNarrow))
      {
#if SCSI_BIOS_SUPPORT
         if (!hhcb->SCSI_HF_biosRunTimeEnabled)
#endif /* SCSI_BIOS_SUPPORT */
         {
            deviceTable->scsiOption |= (SCSI_WIDE_XFER | SCSI_SYNC_XFER);
         }
      }
   }

   /* Turn on narrow negotiation only if the adapter supports only narrow bus */
   else
   {
      if (deviceTable->SCSI_DNF_forceNarrow)
      {
#if SCSI_BIOS_SUPPORT
         if (!hhcb->SCSI_HF_biosRunTimeEnabled)
#endif /* SCSI_BIOS_SUPPORT */
         {
            deviceTable->scsiOption |= (SCSI_WIDE_XFER | SCSI_SYNC_XFER);
         }   
      }
   }

   /* turn on the negotiation initiation */
   SCSI_hXFEROPTASSIGN(hhcb,targetUnit->scsiID,SCSI_NEEDNEGO);

   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
}
#endif /* SCSI_NEGOTIATION_PER_IOB */

/******************************************************************************
*
*  SCSIHDataBusHang
*
*     Check if data bus is hung
*
*  Return Value:  scbNumber : if data bus has hung while executing this SCB
*                 SCSI_NULL_SCB : if there is no active SCB or data bus has not
*                                 hung
*                  
*  Parameters:    hhcb
*
*  Remarks:       it is the responsibility of the caller to make sure the
*                 sequencer is unpaused (if needed) before calling this routine.
*                 This is because this routine just waits for the SCSI bus to
*                 change from data phase to any other phase, and the sequencer
*                 should be running to load all the S/G elements to finish the
*                 data transfer.
*                  
******************************************************************************/
#if SCSI_DOMAIN_VALIDATION
SCSI_UEXACT16 SCSIHDataBusHang (SCSI_HHCB SCSI_HPTR hhcb)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 scsisig;
   SCSI_UEXACT32 timer = (SCSI_UINT32)0x24000;
   SCSI_UEXACT8 hcntrl, i;
   SCSI_UEXACT16 scbNumber;

   /* check if pause chip necessary */
   if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);      
   }

   /* Get the currently active SCB and the scsi control signals */
   scbNumber = ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb)+1))) << 8) |
                (SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_hACTIVE_SCB(hhcb))));
   scsisig = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI));

   /* Unpause, if needed */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb,hcntrl);
   }

   if ((scsisig != 0) && (!(scsisig & (SCSI_CDI | SCSI_MSGI)))) 
   {
      while(timer--)
      {
         /* Wait for about 100 us before snooping the bus */
         for (i=0; i<20; i++)
         {
            OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
         }

         /* check if pause chip necessary */
         if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
         {
            SCSIhPauseAndWait(hhcb);      
         }

         /* Read the signals */
         scsisig = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI));

         /* Unpause, if needed */
         if (!(hcntrl & SCSI_PAUSEACK))
         {
            SCSI_hWRITE_HCNTRL(hhcb,hcntrl);
         }

         /* If we are out of the data phase, we are done */
         if ((scsisig == 0) || ((scsisig & (SCSI_CDI | SCSI_MSGI)))) 
            break;
      }
   }

   /* If the timer did not expire, then there was no hang during data phase;
      so, return null scb */
   if (timer)
   {
      SCSI_SET_NULL_SCB(scbNumber);
   }

   return (scbNumber);
}
#endif /* SCSI_DOMAIN_VALIDATION */

/*********************************************************************
*
*  SCSIHApplyNVData
*
*     Apply NV RAM configuration information to the adapter
*
*  Return Value:  SCSI_SUCCESS - NV data applied
*                 SCSI_FAILURE - No NV ram available
*                  
*  Parameters:    hhcb
*
*  Remarks:
*
*********************************************************************/
#if (!SCSI_BIOS_MODE_SUPPORT)
SCSI_UINT8 SCSIHApplyNVData (SCSI_HHCB SCSI_HPTR hhcb)
{

#if SCSI_SCSISELECT_SUPPORT
   SCSI_HOST_ADDRESS SCSI_LPTR hostAddress;
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_NVM_LAYOUT nvmData;
   SCSI_UINT16 seepromBase;
   SCSI_BOOLEAN validScbFFData = SCSI_FALSE;
   SCSI_UINT i;
#if SCSI_TARGET_OPERATION
   SCSI_DEVICE SCSI_DPTR hostScsiIDdeviceTable;
#endif /* SCSI_TARGET_OPERATION */
#endif /* SCSI_SCSISELECT_SUPPORT */

   /* For right now, by default, the multiple LUN support will be */
   /* turned on for all targets until we come up with a new decision. */
   /* Set here to guarantee that the multiple LUN always support */
   /* even if the checksum of the nvram is bad. */
   hhcb->multipleLun = 0xFFFF;

#if SCSI_SCSISELECT_SUPPORT
   /* try to get from OSM non-volatile memory */
   if (OSD_GET_NVDATA(hhcb,&nvmData,0,sizeof(SCSI_NVM_LAYOUT)))
   {
      /* OSM NVM not available */
      /* read from SEEPROM attached to host device */

      /* figure out the section base within the seeprom */
      seepromBase = 0;

      hostAddress = OSD_GET_HOST_ADDRESS(hhcb);

      if (hhcb->SCSI_HF_multiFunction)
      {
          if (hostAddress->pciAddress.functionNumber != 0)
          {
             seepromBase = sizeof(SCSI_NVM_LAYOUT) / 2;
          }
      }
      else
      {
          if (hhcb->indexWithinGroup)
          {
             seepromBase = sizeof(SCSI_NVM_LAYOUT) / 2;
          }
      }
      
      /* read from seeprom */
      if (SCSIHReadSEEPROM(hhcb,(SCSI_UEXACT8 SCSI_SPTR) &nvmData,
          (SCSI_SINT16) seepromBase,(SCSI_SINT16) sizeof(SCSI_NVM_LAYOUT)/2))
      {
         if (!SCSIhCheckSigSCBFF(hhcb,(SCSI_UEXACT8 SCSI_SPTR) &nvmData))
         { 
            /* seeprom not available and no SCBFF information */
            return((SCSI_UINT8)(SCSI_FAILURE));
         }
         else
         if (!SCSIhDetermineValidNVData((SCSI_NVM_LAYOUT SCSI_SPTR) &nvmData))
         {
            return((SCSI_UINT8)(SCSI_FAILURE));
         }
         else
         {
            validScbFFData = SCSI_TRUE;
         }
      }
      
      /* Harpoon decodes seeprom device type so no need to attempt another type */
      if ((!validScbFFData) &&
          (!SCSIhDetermineValidNVData((SCSI_NVM_LAYOUT SCSI_SPTR) &nvmData)))
      {
         if (!SCSIhCheckSigSCBFF(hhcb,(SCSI_UEXACT8 SCSI_SPTR) &nvmData))
         {
            /* serial eeprom not available and no valid SCBFF data */
            return((SCSI_UINT8)(SCSI_FAILURE));
         }
         else
         if (!SCSIhDetermineValidNVData((SCSI_NVM_LAYOUT SCSI_SPTR) &nvmData))
         {
            return((SCSI_UINT8)(SCSI_FAILURE));
         }
      }
   }

   /* make sure the max. targets in NVRAM matching with the default setting */
   /* value which based on the HA capability. */
   if (hhcb->maxDevices != (SCSI_UEXACT8)(nvmData.word20 & SCSI_WORD20_MAXTARGETS))
   {
      return((SCSI_UINT8)(SCSI_FAILURE));
   }
   
   /* interpret and apply nvm information */
   /* apply per target configuration */

   /* U320 (Harpoon 2) NVRAM targets layout */
   for (i = 0; i < (SCSI_UINT)(nvmData.word20 & SCSI_WORD20_MAXTARGETS); i++)
   {
      deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];

      /* Make NVRAM entry to be current negoXferIndex and defaultXferIndex */
      deviceTable->negoXferIndex = SCSI_NVRAM_ENTRY;
      deviceTable->defaultXferIndex = SCSI_NVRAM_ENTRY;
      deviceTable->maxXferIndex = SCSI_NVRAM_ENTRY;

#if SCSI_DOMAIN_VALIDATION
      /* Inititialize pre-DV default transfer index to defaultXferIndex. */
      deviceTable->preDVXferIndex = deviceTable->defaultXferIndex;
#endif /* SCSI_DOMAIN_VALIDATION */

      /* NOTE: Since there is no longer a single bit that defines ASYNC, */
      /*       a 'mask' check for 0x3F must be performed to determine if */
      /*       the NVData requires that we operate in ASYNC mode.        */
      
      /* load wide bit */
      SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_MISC_OPT] = (SCSI_UEXACT8)
         ((nvmData.targetControl.u320TC[i] & SCSI_U320TC_INITIATEWIDE) >> 12);
      /* load scsi rate period and offset */
      if ((nvmData.targetControl.u320TC[i] & SCSI_U320TC_ASYNCRATEBITS) ==
            SCSI_U320TC_ASYNCRATEBITS)
      {
         /* Async mode */
         SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
         SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;
      }
      else
      {
         /* Sync mode */
         SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_PERIOD] = (SCSI_UEXACT8)
                  (nvmData.targetControl.u320TC[i] & SCSI_U320TC_ASYNCRATEBITS);
#if SCSI_PACKETIZED_IO_SUPPORT
#if SCSI_TARGET_OPERATION
         /* If target mode only operation then reduce the transfer rate
          * fields to match maximum capability - U160 DT, QAS 
          */
         if ((hhcb->SCSI_HF_targetMode) && (!hhcb->SCSI_HF_initiatorMode)
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
                && (!(SCSI_hHARPOON_REV_1B(hhcb)))
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
            )
         {
            if (SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_PERIOD] == 0x08)
            {
               /* Fast-160 - reduce to Fast-80 */
               SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_PERIOD] = (SCSI_UEXACT8)0x09;
            }
         }   
#endif /* SCSI_TARGET_OPERATION */
#else
         /* Non-packetized support */
         /* Make sure appropriate non-packetized transfer period is set */ 
         if (SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_PERIOD] == 0x08)
         {
            /* Fast-160 - reduce to Fast-80 */
            SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_PERIOD] = (SCSI_UEXACT8)0x09;
         }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
         SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_OFFSET] = SCSI_MAX_OFFSET;
      }   

      /* setup DT clocking, Packetized and QAS bit */
      /* setup non NVRAM ppr options to hardcoded defaults*/
      SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] =
        SCSI_HARDCODE_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &
                    ~((SCSI_UEXACT8)(SCSI_DUALEDGE | SCSI_QUICKARB | SCSI_PACKETIZED));

      /* Assume to run DT mode at the xfer period of 0x09 or less.   */
      /* Even though our hardware can run DT at higher period.       */
      /* Due to NVRAM space constrain, our SCSISelect cannot provide */
      /* different xfer rate options for DT.                         */
      if (SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_PERIOD] <= 0x09)
      {
         SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] |= SCSI_DUALEDGE;
      }

#if SCSI_PACKETIZED_IO_SUPPORT
#if SCSI_TARGET_OPERATION
         /* Only load Packetized bit if initiator mode enabled */
         if (hhcb->SCSI_HF_initiatorMode
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
              || ((hhcb->SCSI_HF_targetMode) && (SCSI_hHARPOON_REV_1B(hhcb)))
              /* change to f/w mode */
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
             )
#endif /* SCSI_TARGET_OPERATION */
            /* load Packetized bit */
            SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] |= (SCSI_UEXACT8)
               ((nvmData.targetControl.u320TC[i] & SCSI_U320TC_PACKETIZED) >> 7);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

      /* load QAS bit */
      SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] |= (SCSI_UEXACT8)
            ((nvmData.targetControl.u320TC[i] & SCSI_U320TC_QASSUPPORT) >> 4);

      /* build disconnectEnable */
      deviceTable->SCSI_DF_disconnectEnable =
         (nvmData.targetControl.u320TC[i] & SCSI_U320TC_DISCONNECTENABLE) >> 10;

      /* build hostManaged setting - NTC specific field */
      deviceTable->SCSI_DF_hostManaged =
         (nvmData.targetControl.u320TC[i] & SCSI_U320TC_HOSTMANAGED) >> 15;
   }

#if SCSI_DOMAIN_VALIDATION
   /* apply bios control configuration if DOMAIN VALIDATION enabled, for */
   /* Standard U320 mode only */
   if ((hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320) ||
       (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320))  
   {
      /* Can only check DV bit in NVRAM if Harpoon.
       * Can only enable DV if firmware mode support provided. 
       */
       
      if ((nvmData.biosControl & SCSI_BIOSCONTROL_DOMAINVALIDATION))
      {
#if SCSI_DOMAIN_VALIDATION_ENHANCED
         hhcb->domainValidationMethod = SCSI_DV_ENHANCED;
#else /* !SCSI_DOMAIN_VALIDATION_ENHANCED */
         hhcb->domainValidationMethod = SCSI_DV_BASIC;
#endif /* SCSI_DOMAIN_VALIDATION_ENHANCED */
      }
      else
      {
         hhcb->domainValidationMethod = SCSI_DV_DISABLE;
      }
   }
   else
   {
      hhcb->domainValidationMethod = SCSI_DV_DISABLE;
   }
#endif /* SCSI_DOMAIN_VALIDATION */

   /* apply general control configuration */ 
   hhcb->SCSI_HF_autoTermDetected =
            (nvmData.generalControl & SCSI_GENERALCONTROL_AUTOTERMDETECTED) >> 7;
   hhcb->SCSI_HF_primaryTermLow =
            (nvmData.generalControl & SCSI_GENERALCONTROL_TERMINATIONLOW) >> 1;
   hhcb->SCSI_HF_primaryTermHigh =
            (nvmData.generalControl & SCSI_GENERALCONTROL_TERMINATIONHIGH) >> 2;

   if (hhcb->SCSI_HF_autoTermDetected)
   {
      hhcb->SCSI_HF_primaryAutoTerm =
               (nvmData.generalControl & SCSI_GENERALCONTROL_CABLESENSINGPRIMARY);
   }
   else
   {
      hhcb->SCSI_HF_primaryAutoTerm = 0;
   }

   /* Need to check for the legacy bit of the Subsystem ID Values */
   if (nvmData.generalControl & SCSI_GENERALCONTROL_LEGACYCONNECTOR)
   {
      /* Apply secondary terminations */
      hhcb->SCSI_HF_secondaryTermLow = 
         (nvmData.generalControl & SCSI_GENERALCONTROL_SECONDARYTERMLOW) >> 4;
      hhcb->SCSI_HF_secondaryTermHigh =
         (nvmData.generalControl & SCSI_GENERALCONTROL_SECONDARYTERMHIGH) >> 5;
      if (hhcb->SCSI_HF_autoTermDetected)
      {
         hhcb->SCSI_HF_secondaryAutoTerm =
             (nvmData.generalControl & SCSI_GENERALCONTROL_CABLESENSINGSECOND) >> 3;
      }
   }

   /* Get termination power level (STPWLEVEL) bit */
   hhcb->SCSI_HF_terminationLevel =
               (nvmData.generalControl & SCSI_GENERALCONTROL_STPWLEVEL) >> 6;

#if SCSI_PARITY_PER_IOB
   /* ParityEnable in NVRAM no longer used for per iob parity checking */ 
   hhcb->SCSI_HF_scsiParity = 0;
#else
   hhcb->SCSI_HF_scsiParity = 
                     (nvmData.biosControl & SCSI_BIOSCONTROL_PARITYENABLE) >> 6;
#endif /* SCSI_PARITY_PER_IOB */

   hhcb->SCSI_HF_resetSCSI =
                     (nvmData.biosControl & SCSI_BIOSCONTROL_RESETSCSI) >> 11;
   /* reset SCSI option also needs to be applied for the flag in device table */
   for ( i = 0; i < SCSI_MAXDEV; i++)
   {
      SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DF_resetSCSI =
                     (nvmData.biosControl & SCSI_BIOSCONTROL_RESETSCSI) >> 11;
   }

   /* Set NTC Cluster Enable field. */
   hhcb->SCSI_HF_clusterEnable =
            (nvmData.generalControl & SCSI_GENERALCONTROL_CLUSTERENABLE) >> 15;

   /* apply word 19, 20 configuration */
   /* scsiID, maxTargets, bootLun, bootID ??? */

   /* To cover the case where the host SCSI id might get changed */
   /* in the scam level 2 environment. */

#if !SCSI_DCH_U320_MODE
#if (!SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT)
   if ((SCSIHBiosState(hhcb) != SCSI_BIOS_ACTIVE) &&
       (SCSIHRealModeExist(hhcb) == SCSI_NOT_EXIST))
#else 
   if (SCSIHBiosState(hhcb) != SCSI_BIOS_ACTIVE)
#endif  /* (!SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT) */
#endif /* !SCSI_DCH_U320_MODE */
   {
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
      if (hhcb->SCSI_HF_targetMode) 
      {
         /* revisit - if the hostScsiID changes then we need
          * to alter the mask removing the old hostScsiID
          * value from the mask and adding the new one to
          * the mask.
          */
         if (hhcb->hostScsiID !=
             ((SCSI_UEXACT8)(nvmData.word19 & SCSI_WORD19_SCSIID)))
         {
            hhcb->SCSI_HF_targetAdapterIDMask &= (~(1 << (hhcb->hostScsiID)));
            hhcb->hostScsiID = (SCSI_UEXACT8)(nvmData.word19 & SCSI_WORD19_SCSIID);
            hhcb->SCSI_HF_targetAdapterIDMask |= (1 << (hhcb->hostScsiID));  
         }
      } 
#else
      hhcb->hostScsiID = (SCSI_UEXACT8)(nvmData.word19 & SCSI_WORD19_SCSIID);
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
   }

#if SCSI_TARGET_OPERATION
   if ((hhcb->SCSI_HF_targetMode) && (!hhcb->SCSI_HF_initiatorMode))
   {
      /* Set the following device table entries based on contents */
      /* of the device table entry of the SCSI ID of the adapter  */
      /* SCSI_TARGET_OPERATION ONLY.                              */  
      hostScsiIDdeviceTable = &SCSI_DEVICE_TABLE(hhcb)[hhcb->hostScsiID]; 

      /* U320 (Harpoon 2) NVRAM targets layout */
      for (i = 0; i < (SCSI_UINT)(nvmData.word20 & SCSI_WORD20_MAXTARGETS); i++)
      {
         deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];

         /* Make NVRAM entry to be current negoXferIndex and defaultXferIndex */
         deviceTable->negoXferIndex = SCSI_NVRAM_ENTRY;
         deviceTable->defaultXferIndex = SCSI_NVRAM_ENTRY;
         deviceTable->maxXferIndex = SCSI_NVRAM_ENTRY;

#if SCSI_DOMAIN_VALIDATION
         /* Inititialize pre-DV default transfer index to defaultXferIndex */
         deviceTable->preDVXferIndex = deviceTable->defaultXferIndex;
#endif /* SCSI_DOMAIN_VALIDATION */

         /* NOTE: Since there is no longer a single bit that defines ASYNC, */
         /*       a 'mask' check for 0x3F must be performed to determine if */
         /*       the NVData requires that we operate in ASYNC mode.        */
      
         /* Load wide bit */
         SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_MISC_OPT] =
            SCSI_NVRAM_XFER(hostScsiIDdeviceTable)[SCSI_XFER_MISC_OPT];

         /* Load offset */
         SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_OFFSET] = 
            SCSI_NVRAM_XFER(hostScsiIDdeviceTable)[SCSI_XFER_OFFSET];

         /* Load period. */
         SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_PERIOD] =
            SCSI_NVRAM_XFER(hostScsiIDdeviceTable)[SCSI_XFER_PERIOD];   

         /* Load DT clocking, Packetized and QAS bits */
         SCSI_NVRAM_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] =
            SCSI_NVRAM_XFER(hostScsiIDdeviceTable)[SCSI_XFER_PTCL_OPT];

         /* Build disconnectEnable */
         deviceTable->SCSI_DF_disconnectEnable =
            hostScsiIDdeviceTable->SCSI_DF_disconnectEnable ;

         /* Build hostManaged setting - NTC specific field */
         deviceTable->SCSI_DF_hostManaged =
            hostScsiIDdeviceTable->SCSI_DF_hostManaged;
      }
   }
#endif /* SCSI_TARGET_OPERATION */

   return((SCSI_UINT8)(SCSI_SUCCESS));   
#else /* !SCSI_SCSISELECT_SUPPORT */
   return((SCSI_UINT8)(SCSI_FAILURE));
#endif /* SCSI_SCSISELECT_SUPPORT */
}
#endif /* (!SCSI_BIOS_MODE_SUPPORT)  */

/*********************************************************************
*
*  SCSIHBiosState
*
*     Check the BIOS state
*
*  Return Value:  SCSI_BIOS_ACTIVE      - Bios scan scsi bus and
*                                         hooked int13.
*                 SCSI_BIOS_SCAN        - Bios scan scsi bus but
*                                         not hooked int3.
*                 SCSI_BIOS_NOT_SCAN    - Bios does not scan scsi bus.
*                 SCSI_BIOS_NOT_PRESENT - Bios is not present on the
*                                         host adapter.
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine will try to return best fit of the
*                 BIOS state as possible.  It bases on the BIOS
*                 information stored in SCB FF to determine all
*                 the statuses.  Therefore, if the SCB FF is
*                 corrupted, the return status will be not accurated.
*                 This will be a case of a driver get unload/load
*                 without save/restore the BIOS information via a
*                 call to our SaveState/RestoreState routines.
*
*********************************************************************/
#if !SCSI_DCH_U320_MODE
#if (!SCSI_BIOS_MODE_SUPPORT)
SCSI_UINT8 SCSIHBiosState (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_UINT8 retval = SCSI_BIOS_NOT_PRESENT;   /* assuming BIOS is not there */

#if (SCSI_BIOS_ASPI8DOS || defined(SCSI_INTERNAL))
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;       
   SCSI_UEXACT16 scbptr;

   if (!hhcb->SCSI_HF_chipReset)
   {
      /* Save the HCNTRL value. */
      hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

      /* Pause the chip, if necessary. */
      if (!(hcntrl & SCSI_PAUSEACK))
      {
         SCSIhPauseAndWait(hhcb);
      }

      /* save SCBPTR value */
      scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
              ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

      /* Load scb ptr with 0xFF */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                        (SCSI_UEXACT8)SCSI_U320_SCBFF_PTR);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                        (SCSI_UEXACT8)(SCSI_U320_SCBFF_PTR>>8));

      /* Check for 'BIOS' signature and if BIOS is installed */
      if (((SCSI_UEXACT8) OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+0)) == 'B') &&
          ((SCSI_UEXACT8) OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+1)) == 'I') &&
          ((SCSI_UEXACT8) OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+2)) == 'O') &&
          ((SCSI_UEXACT8) OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+3)) == 'S'))
      {
         if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+SCSI_SCBFF_INFO_BYTE_OFST)) &
             SCSI_INFOBYTE_BIOS_INSTALLED)
         {
            retval = (SCSI_UINT8)SCSI_BIOS_ACTIVE;
         }
         else
         {
            retval = (SCSI_UINT8)SCSI_BIOS_SCAN;
         }
      }
      else if (((SCSI_UEXACT8) OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+0)) == 'A') &&
               ((SCSI_UEXACT8) OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+1)) == 'D') &&
               ((SCSI_UEXACT8) OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+2)) == 'P') &&
               ((SCSI_UEXACT8) OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+3)) == 'T'))
      {
         retval = (SCSI_UINT8)SCSI_BIOS_NOT_SCAN;
      }

      /* restore SCBPTR */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbptr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbptr >> 8));
   
      /* Restore HCNTRL if necessary */
      if (!(hcntrl & SCSI_PAUSEACK))
      {
         SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
      }
   }
#endif /* (SCSI_BIOS_ASPI8DOS || defined(SCSI_INTERNAL)) */
   if (retval == SCSI_BIOS_NOT_PRESENT)
   { 
      hhcb->SCSI_HP_biosPresent = 0;
   } 
   else
   { 
      hhcb->SCSI_HP_biosPresent = 1;
   } 
   return (retval);
}
#endif /* !SCSI_BIOS_MODE_SUPPORT */
#endif /* !SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIHRealModeExist
*
*     Check to see whether Real Mode driver load before/exist or not
*
*  Return Value:  SCSI_EXIST     - real mode driver exist
*                 SCSI_NOT_EXIST - real mode driver does not exist
*                  
*  Parameters:    hhcb
*
*  Remarks:
*
*********************************************************************/
#if !SCSI_DCH_U320_MODE
#if (!SCSI_BEF_SUPPORT && !SCSI_BIOS_SUPPORT && !SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT)
SCSI_UINT8 SCSIHRealModeExist (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_UINT8 retval = SCSI_NOT_EXIST;    /* assuming real mode driver does not exist */

#if (SCSI_BIOS_ASPI8DOS || defined(SCSI_INTERNAL))
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 hcntrl;       
   SCSI_UEXACT16 scbptr;

   if (!hhcb->SCSI_HF_chipReset)
   {
      /* Save the HCNTRL value. */
      hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

      /* Pause the chip, if necessary. */
      if (!(hcntrl & SCSI_PAUSEACK))
      {
         SCSIhPauseAndWait(hhcb);
      }

      /* save SCBPTR value */
      scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
              ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

      /* Load scb ptr with 0xFF */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR),
                         (SCSI_UEXACT8)SCSI_U320_SCBFF_PTR);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),
                         (SCSI_UEXACT8)(SCSI_U320_SCBFF_PTR>>8));

      if (   (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+0)) == 'A')
          && (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+1)) == 'S')
          && (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+2)) == 'P')
          && (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+3)) == 'I'))
      {
         retval = (SCSI_UINT8)SCSI_EXIST;
      }

      /* restore SCBPTR */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbptr);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1),(SCSI_UEXACT8)(scbptr >> 8));
   
      /* Restore HCNTRL if necessary */
      if (!(hcntrl & SCSI_PAUSEACK))
      {
         SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
      }
   }
#endif /* (SCSI_BIOS_ASPI8DOS || defined(SCSI_INTERNAL)) */
   return (retval);
}
#endif /* (!SCSI_BEF_SUPPORT && !SCSI_BIOS_SUPPORT && !SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT) */
#endif /* !SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhDetermineValidNVData
*
*     This routine determines if the NV RAM is valid.
*
*  Return Value:  0 - NV RAM check failed
*                 1 - NV RAM check successful
*
*  Parameters:    nvmdataptr
*
*  Remarks:
*
*********************************************************************/
#if ((SCSI_SCSISELECT_SUPPORT) && (!SCSI_BIOS_MODE_SUPPORT))
SCSI_UEXACT8 SCSIhDetermineValidNVData (SCSI_NVM_LAYOUT SCSI_LPTR nvmdataptr)
{
   SCSI_UEXACT16 sum;
   SCSI_UEXACT8 seepromAllzero = 1;
   SCSI_UEXACT8 seepromAllff = 1;
   SCSI_UEXACT8 seepromdefaultchk = 1;
   SCSI_UINT i;
   SCSI_UEXACT16 SCSI_LPTR databuffer = (SCSI_UEXACT16 SCSI_LPTR) nvmdataptr;
      
   /* check to see if seeprom are all 0s or all 0xFFFFs */
   for (i = 0; i < (SCSI_SINT16) sizeof(SCSI_NVM_LAYOUT)/2; i++)
   {   
      if (databuffer[i] != 0)
      {
         seepromAllzero = 0;
         break;
      }
   }

   if (seepromAllzero)
   {
     seepromdefaultchk = 0;
   }

   for (i = 0; i < (SCSI_SINT16) sizeof(SCSI_NVM_LAYOUT)/2; i++)
   {   
      if (databuffer[i] != 0xFFFF)
      {
         seepromAllff = 0;
         break;
      }
   }
   
   if (seepromAllff)
   {
     seepromdefaultchk = 0;
   }

   /* calculate check sum */
   sum = 0;
   for (i = 0; i < OSDoffsetof(SCSI_NVM_LAYOUT,checkSum)/2; i++)
   {
      sum += databuffer[i];
   }

   /* make sure check sum matched */
   if (sum != (nvmdataptr->checkSum))
   {
     seepromdefaultchk = 0;
   }

   return(seepromdefaultchk);
}
#endif /* ((SCSI_SCSISELECT_SUPPORT) && (!SCSI_BIOS_MODE_SUPPORT)) */

/*********************************************************************
*
*  SCSIHUpdateDeviceTable
*
*     Updates the device table information
*
*  Return Value:  void
*
*  Parameters:    hhcb
*
*  Remarks:       Called by SCSIVerifyAdapter to update the
*                 device table negotiation parameters etc.
*                 Device table information is obtained from
*                 NVRAM or BIOS if available.
*                 This routine must not write to any hardware
*                 registers as it is called prior to HIMInitialize
*                 and HIMSaveState may be called after
*                 HIMVerifyAdapter.
*
*********************************************************************/
#if !SCSI_BIOS_MODE_SUPPORT
void SCSIHUpdateDeviceTable (SCSI_HHCB SCSI_HPTR hhcb)
{
#if (SCSI_BIOS_ASPI8DOS || defined(SCSI_INTERNAL) || SCSI_NEGOTIATION_PER_IOB) 
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_UEXACT8 j;
   SCSI_UINT8 biosState;
#endif /* (SCSI_BIOS_ASPI8DOS || defined(SCSI_INTERNAL) || SCSI_NEGOTIATION_PER_IOB) */
   SCSI_UEXACT8 i;

/* Temporary fix to compile CHIM when SCSI_BIOS_ASPI8DOS is 0.
 * Should be revisited.
 */
#if (SCSI_BIOS_ASPI8DOS || defined(SCSI_INTERNAL))
#if (!SCSI_DOWNSHIFT_MODE && !SCSI_ASPI_SUPPORT)
   if (((biosState = SCSIHBiosState(hhcb)) == SCSI_BIOS_ACTIVE) ||
       (SCSIHRealModeExist(hhcb) == SCSI_EXIST))
#else
   if ((biosState = SCSIHBiosState(hhcb)) == SCSI_BIOS_ACTIVE)
#endif /* (!SCSI_DOWNSHIFT_MODE  && !SCSI_ASPI_SUPPORT) */
   {
      for (i = 0; i < SCSI_MAXDEV; i++)
      {
         /* apply transfer option */
         deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];

         /* Harpoon BIOS */
         /* Use in the case of Real/Protected mode switching */
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
         /* This routine is called before SCSIInitialize, therefore 
          * ENMULTID has not been enabled yet!!! So don't call with
          * other ID.
          */
         /* if (SCSI_hHARPOON_REV_1B(hhcb) &&
             hhcb->SCSI_HF_targetMode &&
             (!hhcb->SCSI_HF_initiatorMode))
         {   */
            /* Eventually a for loop of SCSI enabled IDs would be required. */
         /* SCSI_hGETNEGODATA(hhcb,
                              (SCSI_UEXACT8)(i | (hhcb->hostScsiID << 4)),
                              SCSI_BIOS_REALMODE_XFER(deviceTable));
         }
         else */
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
         {            
            SCSI_hGETNEGODATA(hhcb,i,SCSI_BIOS_REALMODE_XFER(deviceTable));
         }

         /* Since the Real mode software exist, we will use the transfer   */
         /* rate parameters already negotiated by BIOS/REAL mode driver to */
         /* re-negotiate for narrow/async mode on the first PAC's Inquiry  */
         /* cmd or HIM_PROBE.                                              */

         /* Use in the case of re-negotiation for narrow/async mode (DV) */
         /* or for the current xfer rate (the first PAC's Inquiry cmd)   */
         /* since the Real Mode software exist.                          */
         for (j = 0; j < SCSI_MAX_XFER_PARAMETERS; j++)
         {
            SCSI_CURRENT_XFER(deviceTable)[j] = SCSI_BIOS_REALMODE_XFER(deviceTable)[j];
            SCSI_LAST_NEGO_XFER(deviceTable)[j] = SCSI_BIOS_REALMODE_XFER(deviceTable)[j];
            SCSI_TARGET_CAP_XFER(deviceTable)[j] = SCSI_BIOS_REALMODE_XFER(deviceTable)[j];
         }

         /* Make SCSI_LAST_NEGO_XFER entry to be a current negoXferIndex. */
         deviceTable->negoXferIndex = SCSI_LAST_NEGO_ENTRY;

         /* Reset 'resetSCSI' flag for each target */
         deviceTable->SCSI_DF_resetSCSI = 0;
      }
      
      /* The chip has been intialized before CHIM based driver invoke. */
      hhcb->SCSI_HF_chipInitialized = 1;

      /* do not reset the SCSI bus */
      hhcb->SCSI_HF_resetSCSI = 0;

   }
   else
#endif /* (SCSI_BIOS_ASPI8DOS || defined(SCSI_INTERNAL)) */
   {
#if SCSI_TARGET_OPERATION
      if (hhcb->SCSI_HF_targetMode) 
      {
         if (!hhcb->SCSI_HF_initiatorMode)
         {    
            /* The chip has not been intialized before CHIM based driver. */
            hhcb->SCSI_HF_chipInitialized = 0;

            /* Do not reset the SCSI bus if operating in Target Mode ONLY */
            hhcb->SCSI_HF_resetSCSI = 0;

            for (i = 0; i < SCSI_MAXDEV; i++)
            {
               SCSI_DEVICE_TABLE(hhcb)[i].SCSI_DF_resetSCSI = 0;
            }
         }
      }
#endif /* SCSI_TARGET_OPERATION */
#if (SCSI_BIOS_ASPI8DOS || defined(SCSI_INTERNAL))
      if (biosState == SCSI_BIOS_SCAN)
      {
         for (i = 0; i < SCSI_MAXDEV; i++)
         {
            /* apply transfer option since bios scans and negotiates to */
            /* device but cannot install when                           */
            /* a) bios enable but there is no int13 device.             */
            /* b) bios disable and scan bus option enable.              */
            deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];

            /* Since the BIOS already negotiated to device, we will use */
            /* the transfer rate parameters to re-negotiate for narrow/ */
            /* async mode on the first PAC's Inquiry cmd or HIM_PROBE.  */
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
            /* This routine is called before SCSIInitialize, therefore 
             * ENMULTID has not been enabled yet!!! So don't call with
             * other ID.
             */
            /* if (SCSI_hHARPOON_REV_1B(hhcb) &&
                hhcb->SCSI_HF_targetMode &&
                (!hhcb->SCSI_HF_initiatorMode))

               {   */
               /* Eventually a for loop of SCSI enabled IDs would be required. */
               /* SCSI_hGETNEGODATA(hhcb,
                                 (SCSI_UEXACT8)(i | (hhcb->hostScsiID << 4)),
                                 SCSI_LAST_NEGO_XFER(deviceTable));
               SCSI_hGETNEGODATA(hhcb,
                                 (SCSI_UEXACT8)(i | (hhcb->hostScsiID << 4)),
                                 SCSI_TARGET_CAP_XFER(deviceTable));
            }
            else */
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
            {
               SCSI_hGETNEGODATA(hhcb,i,SCSI_LAST_NEGO_XFER(deviceTable));
               SCSI_hGETNEGODATA(hhcb,i,SCSI_TARGET_CAP_XFER(deviceTable));
            }

            /* Make SCSI_LAST_NEGO_XFER entry to be a current negoXferIndex. */
            deviceTable->negoXferIndex = SCSI_LAST_NEGO_ENTRY;
         }
      }
#endif /* (SCSI_BIOS_ASPI8DOS || defined(SCSI_INTERNAL)) */
   }

#if SCSI_NEGOTIATION_PER_IOB
   for (i = 0; i < SCSI_MAXDEV; i++)
   {
      /* apply transfer option */
      deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];

      deviceTable->origOffset = SCSI_MAX_OFFSET;
   }
#endif /* SCSI_NEGOTIATION_PER_IOB */

}
#endif /* !SCSI_BIOS_MODE_SUPPORT */

/*********************************************************************
*
*  SCSIhCompareBusSGListAddress
*
*     Compare two bus SG List addresses.
*
*  Return Value:  0 - both sg addresses match
*                 1 - both sg addressed does not match
*                  
*  Parameters:    Scatter Gatter busAddress1 
*                 Scatter Gatter busAddress2
*
*  Remarks:       This routine compares two Scatter Gatter
*                 busAddresses byte by byte.
*                  
*********************************************************************/
#if ((OSD_BUS_ADDRESS_SIZE == 64) && SCSI_STANDARD_MODE)
SCSI_UINT8 SCSIhCompareBusSGListAddress (SCSI_HHCB SCSI_HPTR hhcb,
                                         SCSI_BUS_ADDRESS busAddress1,
                                         SCSI_BUS_ADDRESS busAddress2)
{
   if (hhcb->SCSI_HF_SgBusAddress32)
   {
      /* Compare on the 32-bit bus address */
      if ((((SCSI_QUADLET SCSI_LPTR) &busAddress1)->u16.order0 != 
           ((SCSI_QUADLET SCSI_LPTR) &busAddress2)->u16.order0) ||
          (((SCSI_QUADLET SCSI_LPTR) &busAddress1)->u16.order1 != 
           ((SCSI_QUADLET SCSI_LPTR) &busAddress2)->u16.order1))
      {
         return(1);
      }

      return(0);
   }

   /* Compare on the 64-bit bus address */
   if ((((SCSI_OCTLET SCSI_LPTR) &busAddress1)->u32.order0 !=
        ((SCSI_OCTLET SCSI_LPTR) &busAddress2)->u32.order0) ||
       (((SCSI_OCTLET SCSI_LPTR) &busAddress1)->u32.order1 !=
        ((SCSI_OCTLET SCSI_LPTR) &busAddress2)->u32.order1))
   {
      return(1);
   }

   return(0);
}
#endif /* ((OSD_BUS_ADDRESS_SIZE == 64) && SCSI_STANDARD_MODE) */

/*********************************************************************
*
*  SCSIHSetupNegoInPacketized
*
*     Set up hardware to re-negotiate in Packetized mode
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 target ID
*
*  Remarks:
*                  
*********************************************************************/
#if SCSI_PACKETIZED_IO_SUPPORT
void SCSIHSetupNegoInPacketized (SCSI_HHCB SCSI_HPTR hhcb,
                                 SCSI_UEXACT8 targetID)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 modeSave;
   SCSI_UEXACT8 hcntrl;

   /* Are we currently in Packetize mode?? */
   if (SCSI_CURRENT_XFER(&hhcb->deviceTable[targetID])[SCSI_XFER_PTCL_OPT] &
       SCSI_PACKETIZED) 
   {
      /* Save the HCNTRL value. */
      hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

      /* Pause the chip, if necessary. */
      if (!(hcntrl & SCSI_PAUSEACK))
      {
         SCSIhPauseAndWait(hhcb);
      }

      /* Save the current mode and set to mode 3 */
      modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
      SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
      /* May not need this for target mode as this routine may not be called. */
      if (SCSI_hHARPOON_REV_1B(hhcb) &&
          hhcb->SCSI_HF_targetMode &&
          (!hhcb->SCSI_HF_initiatorMode))
      {
         /* Set the upper 4 bits of NEGODAT to our SCSI ID for 
          * multiple target ID support.
          */
         /* Offset to location in auto-rate-offset table by specified target ID */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGOADDR),
                       (targetID | (hhcb->hostScsiID << 4)));
      }
      else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
      {
         /* Offset to location in auto-rate-offset table by specified target ID */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGOADDR), targetID);
      }

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

      /* Restore mode pointer */
      SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

      /* UnPause if necessary */
      if (!(hcntrl & SCSI_PAUSEACK))
      {
         SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
      }
   }   
}
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

/*********************************************************************
*
*  SCSIhStandardSetElecInformation
*
*     Set up hardware SCSI signal slew rate, amplitude and precomp
*     Cutback level for this target.
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 target ID
*                 xferRate
*
*  Remarks:       called from SCSIhPutProfileParameters and
*                 SCSIhLoadNegoData
*                  
*********************************************************************/
#if SCSI_STANDARD_MODE
void SCSIhStandardSetElecInformation (SCSI_HHCB SCSI_HPTR hhcb,
                                      SCSI_UEXACT8 targetID,
                                      SCSI_UEXACT8 SCSI_LPTR xferRate)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_UEXACT8 annexdat;
   SCSI_UEXACT8 modeSave;
   SCSI_UEXACT8 hcntrl;
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
   SCSI_UEXACT8 enmultid;
   SCSI_UEXACT8 temp;
   SCSI_UEXACT8 negodat0;
   SCSI_UEXACT8 negodat2;
   SCSI_UEXACT8 negodat3;
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */

   /* Save the HCNTRL value. */
   hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL));

   /* Pause the chip, if necessary. */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
   /* When SCSI_MULTIPLEID_SUPPORT is enabled the targetID parameter
    * contains 'our ID' in the upper 4 bits. Need to mask
    * these bits out when H1B ASIC doesn't have ENMULTID
    * enabled.
    */
   enmultid = 0;
   if (SCSI_hHARPOON_REV_1B(hhcb) &&
       hhcb->SCSI_HF_targetMode &&
       (!hhcb->SCSI_HF_initiatorMode))
   {
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL2)) & SCSI_ENMULTID)
      {
         /* Set an indicator that ENMULTID = 1 */
         enmultid = 1;
      }
      else
      {
         /* Mask off the upper 4 bits in targetID parameter */
         targetID &= (~SCSI_OWN_NEGOADDR);
      }   
   }
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */

   /* Offset to location in auto-rate-offset table by specified target ID */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGOADDR), targetID);

#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
   if (enmultid)
   {
      /* The upper bits of targetID can now be masked NEGOADDR is set. */
      targetID &= (~SCSI_OWN_NEGOADDR);

      /* Initialize ANNEXCOL to point to per-device byte 0 location. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXCOL), SCSI_MTID_PERDEVICE0);
   }
   else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
   {
      /* Initialize ANNEXCOL to point to per-device byte 0 location. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXCOL), SCSI_PERDEVICE0);
   }
   
   deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetID];

#if SCSI_STANDARD_U320_MODE
   if (SCSI_hHARPOON_REV_A(hhcb))
   {
      if (!deviceTable->SCSI_DEP_userSlewRate)
      {
         if (xferRate[SCSI_XFER_PERIOD] <= 0x08)
         {
#if SCSI_OEM1_SUPPORT
            if (hhcb->OEMHardware == SCSI_OEMHARDWARE_OEM1)
            {
               annexdat = 0;
            }
            else
#endif /* SCSI_OEM1_SUPPORT */
            {
               annexdat = SCSI_SLEWRATE;
            }
         }
         else
         {
            annexdat = 0;
         }  
      }
#if SCSI_ELEC_PROFILE     
      else
      {
         /* The user specified the slewrate through a profile */

         if (deviceTable->SCSI_DEP_slewrate == SCSI_SLEWRATE_FAST)
         {
            annexdat = SCSI_SLEWRATE;
         }
         else
         {
            annexdat = 0;
         }
      }
#endif /* SCSI_ELEC_PROFILE */     

      /* Definition of the three precomp control bits:            */
      /*   Bits 2-0: 111 - precomp on, 37% cutback                */
      /*             110 - precomp on, 29% cutback                */
      /*             100 - precomp on, 17% cutback                */
      /*             000 - precomp off                            */

#if !SCSI_FORCE_PRECOMP
      if ((xferRate[SCSI_XFER_PTCL_OPT] & SCSI_PCOMP_EN)
            && (xferRate[SCSI_XFER_PERIOD] <= 0x08))
#endif /* !SCSI_FORCE_PRECOMP */
      {
         annexdat |= (SCSI_UEXACT8) deviceTable->SCSI_DEP_precomp;
      }

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXDAT), (SCSI_UEXACT8)annexdat);
   }
   else
#endif /* SCSI_STANDARD_U320_MODE */
   {
      /* RevB Slew Rate is bits 3-6 */

      if (!deviceTable->SCSI_DEP_userSlewRate)
      {
#if SCSI_OEM1_SUPPORT
         if (hhcb->OEMHardware == SCSI_OEMHARDWARE_OEM1)
         {
            annexdat = (SCSI_UEXACT8)SCSI_DEFAULT_OEM1_HARPOON2B_SLEWRATE << 3;
         }
         else
#endif /* SCSI_OEM1_SUPPORT */
         {
            annexdat =  (SCSI_UEXACT8)SCSI_DEFAULT_HARPOON2B_SLEWRATE << 3;
         }
      }
#if SCSI_ELEC_PROFILE     
      else
      {
         /* The user specified the slewrate through a profile */
         annexdat = (SCSI_UEXACT8) deviceTable->SCSI_DEP_slewrate << 3;
      }
#endif /* SCSI_ELEC_PROFILE */     

      /* Definition of the three precomp control bits:            */
      /*   Bits 2-0: 111 - precomp on, 37% cutback                */
      /*             110 - precomp on, 29% cutback                */
      /*             100 - precomp on, 17% cutback                */
      /*             000 - precomp off                            */

#if !SCSI_FORCE_PRECOMP
      if ((xferRate[SCSI_XFER_PTCL_OPT] & SCSI_PCOMP_EN)
            && (xferRate[SCSI_XFER_PERIOD] <= 0x08))
#endif /* SCSI_FORCE_PRECOMP */
      {
         annexdat |= (SCSI_UEXACT8) deviceTable->SCSI_DEP_precomp;
      }

#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
      if (enmultid)
      {
         /* ENMULTID is enabled. */

         /* There is a bug in the H1B ASIC that results in the
          * NEGODATx registers being overwritten when
          * ANNEXDAT is written. For example, writing to
          * ANNEXDAT when ANNEXCOL is set to PERDEVICE0 will
          * also result in NEGODAT0 being updated with this
          * value. Therefore the NEGODATx registers must be
          * saved and restored prior to writing to ANNEXDAT
          * equivalents.
          *
          *  ANNEXCOL = 0    NEGODAT0
          *  ANNEXCOL = 1    NEGODAT1
          *  ANNEXCOL = 2    NEGODAT2
          *  ANNEXCOL = 3    NEGODAT3
          *
          * This is H1B Razor issue #46.
          */
         /* Save NEGODAT0, NEGODAT2, and NEGODAT3 */
         negodat0 = OSD_INEXACT8(SCSI_AICREG(SCSI_NEGODATA0));
         negodat2 = OSD_INEXACT8(SCSI_AICREG(SCSI_NEGODATA2));
         negodat3 = OSD_INEXACT8(SCSI_AICREG(SCSI_NEGODATA3));

         temp = OSD_INEXACT8(SCSI_AICREG(SCSI_ANNEXDAT));
  
         /* ANNEXCOL auto-increments so need to restore */
         /* Initialize ANNEXCOL to point to per-device byte 0 location. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXCOL), SCSI_MTID_PERDEVICE0);

         /* Only precomp in perdevice0 */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXDAT),
                       (temp | (annexdat & SCSI_PCOMP)));

         /* Slew rate is in perdevice3 */
         /* Initialize ANNEXCOL to point to per-device byte 3 location. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXCOL), SCSI_MTID_PERDEVICE3);
         temp = OSD_INEXACT8(SCSI_AICREG(SCSI_ANNEXDAT));

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXCOL), SCSI_MTID_PERDEVICE3);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXDAT),
                       (( temp & (~SCSI_MTID_SLEWRATE)) |
                        (annexdat >> 3)));

         /* Initialize ANNEXCOL to point to per-device byte 2 location. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXCOL), SCSI_MTID_PERDEVICE2);
  
         /* Amplitude is Bits 2:0 */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXDAT),
                       (SCSI_UEXACT8)deviceTable->SCSI_DEP_amplitude);

         /* Restore NEGODATx registers that were corrupted */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA0), negodat0);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA2), negodat2);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_NEGODATA3), negodat3);
      }
      else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
      {   
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXDAT), (SCSI_UEXACT8)annexdat);
  
         /* Initialize ANNEXCOL to point to per-device byte 2 location. */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXCOL), SCSI_PERDEVICE2);

         /* Amplitude is Bits 2:0 */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXDAT),
                       (SCSI_UEXACT8)deviceTable->SCSI_DEP_amplitude);
      }
   }

#if SCSI_PROFILE_INFORMATION
   /* Save data of per-device byte 0, 1, 2 and 3 for later use in getting     */
   /* adapter/target profile.                                                 */
   /* CHIM has been updated to not access hardware register for some value    */
   /* during adapter/target profile request.  The change is to fix IO timeout */
   /* exhibit on Harpoon 2 Rev A with the sequencer workaround, ww19, when    */
   /* OSM requests target profile frequently.                                 */
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
   if (enmultid)
   {
      /* Initialize ANNEXCOL to point to per-device byte 0 location. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXCOL), SCSI_MTID_PERDEVICE0);
   }
   else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
   {
      /* Initialize ANNEXCOL to point to per-device byte 0 location. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXCOL), SCSI_PERDEVICE0);
   }

   /* Obtain data of per-device byte 0, 1, 2 and 3. */
   /* ANNEXCOL is auto-increment so no need to setup subsequent byte. */
   deviceTable->annexdatPerDevice0 = OSD_INEXACT8(SCSI_AICREG(SCSI_ANNEXDAT));
   deviceTable->annexdatPerDevice1 = OSD_INEXACT8(SCSI_AICREG(SCSI_ANNEXDAT));
   deviceTable->annexdatPerDevice2 = OSD_INEXACT8(SCSI_AICREG(SCSI_ANNEXDAT));
   deviceTable->annexdatPerDevice3 = OSD_INEXACT8(SCSI_AICREG(SCSI_ANNEXDAT));
#endif /* SCSI_PROFILE_INFORMATION */

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   /* Restore HCNTRL, if necessary. */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
}
#endif /* SCSI_STANDARD_MODE */

/*********************************************************************
*
*  SCSIhDownshiftSetElecInformation
*
*     Set up hardware SCSI signal slew rate, amplitude and precomp
*     Cutback level for this target.
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 target ID
*                 xferRate
*
*  Remarks:       called from SCSIhPutProfileParameters and
*                 SCSIhLoadNegoData
*                  
*********************************************************************/
#if SCSI_DOWNSHIFT_MODE
void SCSIhDownshiftSetElecInformation (SCSI_HHCB SCSI_HPTR hhcb,
                                       SCSI_UEXACT8 targetID,
                                       SCSI_UEXACT8 SCSI_LPTR xferRate)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable;
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

   deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetID];

   /* Initialize ANNEXCOL to point to per-device byte 0 location. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXCOL), SCSI_PERDEVICE0);

   if (SCSI_hHARPOON_REV_A(hhcb))
   {

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXDAT), 0);
   }
   else
   {
      /* RevB Slew Rate is bits 3-6 */

      /* Definition of the three precomp control bits:            */
      /*   Bits 2-0: 111 - precomp on, 37% cutback                */
      /*             110 - precomp on, 29% cutback                */
      /*             100 - precomp on, 17% cutback                */
      /*             000 - precomp off                            */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXDAT),  (SCSI_UEXACT8)SCSI_DEFAULT_HARPOON2B_SLEWRATE << 3);
   
      /* Initialize ANNEXCOL to point to per-device byte 2 location. */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXCOL), SCSI_PERDEVICE2);

      /* Amplitude is Bits 2:0 */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_ANNEXDAT),
                    (SCSI_UEXACT8)deviceTable->SCSI_DEP_amplitude);
   }

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);

   /* Restore HCNTRL, if necessary. */
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
}
#endif /* SCSI_DOWNSHIFT_MODE */ 



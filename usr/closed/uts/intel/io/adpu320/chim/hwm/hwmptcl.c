 /*$Header: /vobs/u320chim/src/chim/hwm/hwmptcl.c   /main/147   Sun May 25 09:01:10 2003   spal3094 $*/

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
*  Module Name:   HWMPTCL.C
*
*  Description:
*                 Codes to implement protocol specific for  hardware 
*                 management module
*
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         NONE
*
***************************************************************************/

#include "scsi.h"
#include "hwm.h"

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
/* Modules internal to hardware management layer                          */
/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
#if !SCSI_ASYNC_NARROW_MODE
/*********************************************************************
*
*  SCSIhNegotiate
*
*     Handle negotiation needed interrupt
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*                 Parameter hiob will be posted to OSM if
*                 SCSIhBadSeq was called for error handling.
*                 Therefore, hiob should not be referenced
*                 on return.
*
*                 Assume we are in Mode 3.
*
*********************************************************************/
void SCSIhNegotiate (SCSI_HHCB SCSI_HPTR hhcb,
                     SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
   SCSI_UEXACT8 scsiID = (SCSI_UEXACT8) SCSI_TARGET_UNIT(hiob)->scsiID;
#if SCSI_PPR_ENABLE
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 scsidatl;
#endif /* SCSI_PPR_ENABLE */

   /* could be the target is asking for retry of message  */
   if (!(OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & 0x10)) 
   {
      /* set reentrant address for sequencer */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                    (SCSI_UEXACT8) (SCSI_hRETRY_IDENTIFY_ENTRY(hhcb) >> 2));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                    (SCSI_UEXACT8) (SCSI_hRETRY_IDENTIFY_ENTRY(hhcb) >> 10));
      return;
   }

   /* set reentrant address for sequencer */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                 (SCSI_UEXACT8) (SCSI_hSIOSTR3_ENTRY(hhcb) >> 2));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                 (SCSI_UEXACT8) (SCSI_hSIOSTR3_ENTRY(hhcb) >> 10));


   /* must be message out to initiate negotiation */
   if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_BUSPHASE) !=

        SCSI_MOPHASE)
   {
      /* default to async/narrow mode */
      SCSIhAsyncNarrowXferRateAssign(hhcb,scsiID);
      return;
   }      

   switch (hiob->SCSI_IP_negoState)
   {
#if SCSI_PPR_ENABLE
      case SCSI_RESPONSE_PPR:
         /* send messages built to target */
         /* xfer_option already updated during handling extended msg in. */
         if (SCSIhSendMessage(hhcb,hiob,SCSI_PPR_MSG_LENGTH) == ((SCSI_UEXACT8)-1))
         {
            /* SCSIhBadSeq was called so just terminate */
            return;
         }
         hiob->SCSI_IP_negoState = SCSI_NOT_NEGOTIATING;
         break;
#endif /* SCSI_PPR_ENABLE */

      case SCSI_RESPONSE_WIDE:
      case SCSI_RESPONSE_MSG07_WIDE:
         /* send messages built to target */
         /* xfer_option already updated during handling extended msg in. */
         if (SCSIhSendMessage(hhcb,hiob,SCSI_WDTR_MSG_LENGTH) == ((SCSI_UEXACT8)-1))
         {
            /* SCSIhBadSeq was called so just terminate */
            return;
         }

         /* If current SCSI_IP_negoState is SCSI_RESPONSE_MSG07_WIDE,    */
         /* leave it unchanged.  This allows the logic later in          */
         /* SCSIhRespondToSync() to pick up as needed to change to       */
         /* SCSI_RESPONSE_MSG07_SYNC state.                              */
         if (hiob->SCSI_IP_negoState != SCSI_RESPONSE_MSG07_WIDE)
         {
            hiob->SCSI_IP_negoState = SCSI_NOT_NEGOTIATING;
         }
         break;
         
      case SCSI_RESPONSE_SYNC:
      case SCSI_RESPONSE_MSG07_SYNC:
         /* send messages built to target */
         /* xfer_option already updated during handling extended msg in. */
         if (SCSIhSendMessage(hhcb,hiob,SCSI_SDTR_MSG_LENGTH) == ((SCSI_UEXACT8)-1))
         {
            /* SCSIhBadSeq was called so just terminate */
            return;
         }
         hiob->SCSI_IP_negoState = SCSI_NOT_NEGOTIATING;
         break;

      default:       /* must be a trully sync. nego. needed */

#if SCSI_PACKETIZED_IO_SUPPORT
         /* Save the last SCSI_CURRENT_XFER's packetized bit.   Because  */
         /* SCSI_CURRENT_XFER entry will be set to async/narrow mode at  */
         /* the begining of every initiator's negotiation.  The reason   */
         /* for the async/narrow mode setting is if the failure occurred */
         /* during negotiation, then CHIM will automatically run in      */
         /* async/narrow mode which is complied with SCSI specification. */
         /* Saving the packetized bit will help CHIM later to determine  */
         /* if after a PPR negotiation, the SCSI transfer mode has been  */
         /* changed from packetized to non-packetized and vice versa or  */
         /* stayed in packetized mode.                                   */
         /* Here we're using SCSI_HF_switchHappened flag for saving the  */
         /* packetized bit.  This flag will be updated if the IU bit in  */
         /* target's PPR message was set during SCSIhVerifyPPRResponse.  */
         /* And the switch really happened if the one of the following   */
         /* conditions is true:                                          */
         /*   1) SCSI_HF_switchHappened = 0 and target's IU bit = 1      */
         /*   2) SCSI_HF_switchHappened = 1 and target's IU bit = 0      */
         /*   3) SCSI_HF_switchHappened = 1 and target's IU bit = 1      */
         hhcb->SCSI_HF_switchHappened =
           SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] & SCSI_PACKETIZED;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */


#if SCSI_BIOS_SUPPORT
         if (((hiob->SCSI_IP_negoState) == SCSI_INITIATE_SYNC) &&
             (!hhcb->SCSI_HF_biosRunTimeEnabled))
#else
         if ((hiob->SCSI_IP_negoState) == SCSI_INITIATE_SYNC)
#endif /* SCSI_BIOS_SUPPORT */
         {
            /* Update current xfer rate entry to be async and narrow mode */
            SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
            SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;
            SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] = 0;
            SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_MISC_OPT] &= SCSI_WIDE;

            /* Update hardware NEGODATA table */
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
            if (SCSI_hHARPOON_REV_1B(hhcb) &&
                hhcb->SCSI_HF_targetMode &&
                (!hhcb->SCSI_HF_initiatorMode))
            {
               /* Eventually a for loop of SCSI enabled IDs would be required. */
               SCSI_hLOADNEGODATA(hhcb,
                                  (SCSI_UEXACT8)(scsiID | (hhcb->hostScsiID << 4)),
                                  SCSI_CURRENT_XFER(deviceTable));
            }
            else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
            {
               SCSI_hLOADNEGODATA(hhcb,scsiID,SCSI_CURRENT_XFER(deviceTable));
            }
         }
         else
         {
            /* default to async/narrow mode */
            SCSIhAsyncNarrowXferRateAssign(hhcb,scsiID);
         }

#if SCSI_PPR_ENABLE  
         deviceTable->SCSI_DF_negotiateDtc = 0; 

         /* Can we start PPR negotiation? */
         if (SCSI_hPPRNEGONEEDED(deviceTable,scsiRegister))
         {
            deviceTable->SCSI_DF_negotiateDtc = 1;

            /* must initiate PPR message */
            if (SCSIhInitiatePPR(hhcb,hiob) == ((SCSI_UEXACT8)-1))
            {
               /* PPR failed which resulted in SCSIhBadSeq call */ 
                return;
            }

            /* check msg reject */
            if ((phase = SCSIhWait4Req(hhcb)) == SCSI_MIPHASE)
            {
               /* if target responds with message reject */
               if (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIBUSL)) == SCSI_MSG07)
               {
                  /* Indicate no DT was negotiated */
                  deviceTable->SCSI_DF_negotiateDtc = 0;

                  /* Can we start WDTR or SDTR negotiation? */
                  if (SCSI_hWIDEORSYNCNEGONEEDED(deviceTable))
                  {
                     /* assert ATN and ack the last msg byte received */
                     OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE | SCSI_ATNO);
                     scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
               
                     /* match the phase */                           
                     if (SCSIhWait4Req(hhcb) == (SCSI_UEXACT8)-1)
                     {
                        SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID15);
                        return;
                     }

                     OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO),
                       OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_BUSPHASE);

                     /* Target gives us a fault support or CHIM runs with the */
                     /* default setting.  We need to clear the target's       */
                     /* DTC support so that later the SCSI xfer rate will be  */
                     /* properly caculated */
                     deviceTable->SCSI_DF_negotiateDtc = 0;
                  
                     /* continue with normal wide/synchronous negotiation */
                     /* procedure */
                     SCSIhNegotiateWideOrSync(hhcb,hiob);
                  }
                  else
                  {
                     /* Ack the last msg byte received. */
                     /* Handle the case where a device is wide but does not */
                     /* support sync. or sync. disabled via target profile. */
                     scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
                     
                     /* Done with negotiation */
                     hiob->SCSI_IP_negoState = SCSI_NOT_NEGOTIATING;
                  }
               }
            }
            else if (phase == (SCSI_UEXACT8)-1)
            {
               SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID16);
               return;
            }
         }
         else
         {
            /* use the normal wide and sync. negotiation message */
            SCSIhNegotiateWideOrSync(hhcb,hiob);
         }
#else  /* !SCSI_PPR_ENABLE */
         /* use the normal wide and sync. negotiation message */
         SCSIhNegotiateWideOrSync(hhcb,hiob);
#endif /* SCSI_PPR_ENABLE */

         break;
   }  /* end of switch (hiob->SCSI_IP_negoState) */
}
#endif /* !SCSI_ASYNC_NARROW_MODE */

#if !SCSI_ASYNC_NARROW_MODE
/*********************************************************************
*
*  SCSIhNegotiateWideOrSync
*
*     Negotiate for Wide and/or Sync. transfer
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
void SCSIhNegotiateWideOrSync (SCSI_HHCB SCSI_HPTR hhcb,
                               SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 scsidatl;
#if SCSI_DOWNSHIFT_MODE
   SCSI_UEXACT8 negoOnModeSwitch;
#endif /* SCSI_DOWNSHIFT_MODE */

#if SCSI_DOWNSHIFT_MODE
   /* For the downshift U320 mode, get the specific target's negoOnModeSwitch */
   /* flag stored in scratch RAM, SCSI_NEGO_ON_MODE_SWITCH.  Most likely, the */
   /* flag is updated during the mode context switch (switch to Real mode).   */
   if (SCSI_TARGET_UNIT(hiob)->scsiID < 8)
   {
      if (negoOnModeSwitch = 
         (SCSI_UEXACT8) (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_NEGO_ON_MODE_SWITCH)) &
                        (1 << SCSI_TARGET_UNIT(hiob)->scsiID)))
         /* clear the nego on mode switch for specified target */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_NEGO_ON_MODE_SWITCH),(0xff & ~negoOnModeSwitch));
   }
   else
   {
      if (negoOnModeSwitch = 
         (SCSI_UEXACT8) (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_NEGO_ON_MODE_SWITCH+1)) &
                        (1 << (SCSI_TARGET_UNIT(hiob)->scsiID - 8))))
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_NEGO_ON_MODE_SWITCH+1),0xff & ~negoOnModeSwitch);
   }
#endif /* SCSI_DOWNSHIFT_MODE */


   /* Can we start WDTR negotiation? */
#if SCSI_DOWNSHIFT_MODE
   if ((SCSI_hWIDENEGONEEDED(deviceTable) || negoOnModeSwitch) && (hiob->SCSI_IP_negoState != SCSI_INITIATE_SYNC))
#else
   if ((SCSI_hWIDENEGONEEDED(deviceTable) || deviceTable->SCSI_DF_negoOnModeSwitch) && (hiob->SCSI_IP_negoState != SCSI_INITIATE_SYNC))
#endif /* SCSI_DOWNSHIFT_MODE */

   {
#if SCSI_STANDARD_MODE
      /* Clear SCSI_DF_negoOnModeSwitch after the first negotiation */
      deviceTable->SCSI_DF_negoOnModeSwitch = 0;
#endif /* SCSI_STANDARD_MODE */
      /* must initiate wide negotiation first */
      if (SCSIhInitiateWide(hhcb,hiob) == ((SCSI_UEXACT8)-1))
      {
         /* an error occured and SCSIhBadSeq was called */
         return;
      }

      /* check msg reject */
      if ((phase = SCSIhWait4Req(hhcb)) == SCSI_MIPHASE)
      {
         /* if target responds with message reject */
         if (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIBUSL)) == SCSI_MSG07)
         {
#if SCSI_BIOS_SUPPORT
            if (!hhcb->SCSI_HF_biosRunTimeEnabled)
#endif /* SCSI_BIOS_SUPPORT */
            {
               SCSI_NEGO_XFER(deviceTable)[SCSI_XFER_MISC_OPT] &=~ SCSI_WIDE;
               SCSI_LAST_NEGO_XFER(deviceTable)[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
            }

            /* Can we start SDTR negotiation? */
            if (SCSI_hSYNCNEGONEEDED(deviceTable))
            {
               /* assert ATN and ack the last msg byte received */
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE | SCSI_ATNO);
               scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
               
               /* match the phase */                           
               if (SCSIhWait4Req(hhcb) == (SCSI_UEXACT8)-1)
               {
                  SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID17);
                  return;
               }

               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO),
                   OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_BUSPHASE);
                  
               /* continue with initiating synchronous negotiation procedure */
               SCSIhInitiateSync(hhcb,hiob);
            }
            else
            {
               /* Ack the last msg byte received. */
               /* Handle the case where a device is wide but does not */
               /* support sync. or sync. disabled via target profile. */
               scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
                     
               /* Done with negotiation */
               hiob->SCSI_IP_negoState = SCSI_NOT_NEGOTIATING;
            }
         }
      }
      else if (phase == (SCSI_UEXACT8)-1)
      {
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID18);
         return;
      }
   }
   else
   {
#if SCSI_NEGOTIATION_PER_IOB
      if (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_MISC_OPT] & SCSI_WIDE)
      {
         /* initiate synchronous */
         SCSIhInitiateSync(hhcb,hiob);

         /* Need CLEAN UP */
         /* restore wide xfer bit in scsiOption that was turn off previously */
         /* deviceTable->scsiOption |= SCSI_WIDE_XFER; */
      }
      else
#endif /* SCSI_NEGOTIATION_PER_IOB */
      {
         /* initiate synchronous first only if wide is not on */
         SCSIhInitiateSync(hhcb,hiob);
      }
   }
}
#endif /* !SCSI_ASYNC_NARROW_MODE */

/*********************************************************************
*
*  SCSIhExtMsgi
*
*     Handle extended message from target interrupt
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*                 Assume we are in Mode 3.
*
*********************************************************************/
void SCSIhExtMsgi (SCSI_HHCB SCSI_HPTR hhcb,
                   SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 count;
   SCSI_UEXACT8 bytesReceived;
#if SCSI_PPR_ENABLE
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 scsidatl;
#endif /* SCSI_PPR_ENABLE */

   /* get length field of extended message */ 
   count = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_hPASS_TO_DRIVER(hhcb)));

   /* collect remainning extended messages */
   bytesReceived = SCSIhReceiveMessage(hhcb,hiob,count); 
   if (bytesReceived != count)
   {
      /* something wrong */
      /* Indicate Ack is needed for the last message byte, which is used */
      /* when CHIM acknowledges the last byte */
      if (bytesReceived != ((SCSI_UEXACT8) -1))
      {
         hiob->SCSI_IP_ackLastMsg = SCSI_ACK_NEEDED;
      }
      return;
   }

   /* Modify Data Pointer Message */
   if (hiob->SCSI_IP_workArea[0] == SCSI_MSGMDP)
   {
      SCSIhModifyDataPtr(hhcb,hiob);
   }
   /* Assume PPR, WDTR, or SDTR Negotiation Message */
   else
   {
      /* check current negotiation state */
      switch(hiob->SCSI_IP_negoState)
      {
#if !SCSI_ASYNC_NARROW_MODE
#if SCSI_PPR_ENABLE
         case SCSI_INITIATE_PPR:
            /* just initiated PPR, must be response from target */
            if (hiob->SCSI_IP_workArea[0] == SCSI_MSGPPR)
            {
               SCSIhVerifyPPRResponse(hhcb,hiob);
            }
            break;
#endif /* SCSI_PPR_ENABLE */

         case SCSI_INITIATE_WIDE:
            /* just initiated WDTR, must be response from target */
            if (hiob->SCSI_IP_workArea[0] == SCSI_MSGWIDE)
            {
               SCSIhVerifyWideResponse(hhcb,hiob);
            }
            break;

         case SCSI_INITIATE_SYNC:
            /* just initiated SDTR, must be response from target */
            if (hiob->SCSI_IP_workArea[0] == SCSI_MSGSYNC)
            {
               SCSIhVerifySyncResponse(hhcb,hiob);
            }
            break;
#endif /* !SCSI_ASYNC_NARROW_MODE */

         default:
#if SCSI_PPR_ENABLE
            /* must be ext messages initiated from target */
            if (hiob->SCSI_IP_workArea[0] == SCSI_MSGPPR)
            {
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE | SCSI_ATNO);
               do {
                  scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
                  phase = SCSIhWait4Req(hhcb);
               } while (phase == SCSI_MIPHASE);
               if (phase != SCSI_MOPHASE)
               {
                  SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID42);
                  return;
               }
               else
               {
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MOPHASE);
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);
                  OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL), SCSI_MSG07);
                  if (SCSIhWait4Req(hhcb) == (SCSI_UEXACT8)-1)
                  {
                     SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID43);
                     return;
                  }
               }
            }
            else if (hiob->SCSI_IP_workArea[0] == SCSI_MSGWIDE)
#else  /* !SCSI_PPR_ENABLE */
            /* must be ext messages initiated from target */
            if (hiob->SCSI_IP_workArea[0] == SCSI_MSGWIDE)
#endif /* SCSI_PPR_ENABLE */
            {
               /* respond to WDTR negotiation */
               SCSIhRespondToWide(hhcb,hiob);
            }
            else if (hiob->SCSI_IP_workArea[0] == SCSI_MSGSYNC)
            {
               /* respond to SDTR negotiation */
               SCSIhRespondToSync(hhcb,hiob);
            }

            break;
      }  /* end of switch */
   }
}

/*********************************************************************
*
*  SCSIhInitiatePPR
*
*     Initiate Parallel Protocol Request message process
*
*  Return Value:  -1 if SCSIhBadSeq was called for error 
*                 recovery 
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*                 Parameter hiob will be posted to OSM if
*                 SCSIhBadSeq was called for error handling.
*                 Therefore, hiob should not be referenced
*                 on return.
*
*********************************************************************/
#if (SCSI_PPR_ENABLE && !SCSI_ASYNC_NARROW_MODE)
SCSI_UEXACT8 SCSIhInitiatePPR (SCSI_HHCB SCSI_HPTR hhcb,
                               SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_UEXACT8 scsiID = (SCSI_UEXACT8) SCSI_TARGET_UNIT(hiob)->scsiID;
   SCSI_UEXACT8 xferRate[SCSI_MAX_XFER_PARAMETERS];
   SCSI_DEVICE SCSI_DPTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[scsiID];

   /* Indicate initiate PPR nego state */
   hiob->SCSI_IP_negoState = SCSI_INITIATE_PPR;

   /* Get nego xfer rate */
   SCSI_hGETNEGOXFERRATE(hhcb,scsiID,xferRate);

   /* Build parallel protocol request message based on configuration */
   hiob->SCSI_IP_workArea[0] = SCSI_MSG01;
   hiob->SCSI_IP_workArea[1] = 6;                     /* extended msg length */
   hiob->SCSI_IP_workArea[2] = SCSI_MSGPPR;
   hiob->SCSI_IP_workArea[3] = xferRate[SCSI_XFER_PERIOD];
   hiob->SCSI_IP_workArea[4] = 0;                     /* reserved byte */
   hiob->SCSI_IP_workArea[5] = xferRate[SCSI_XFER_OFFSET];
   hiob->SCSI_IP_workArea[6] = 
                     (SCSI_UEXACT8) (xferRate[SCSI_XFER_MISC_OPT] & SCSI_WIDE);

   /* Setup Protocol Options field                                       */
   /*  Bit  Definition  Meaning                                          */
   /*  ---  ----------  --------                                         */
   /*   0   IU_REQ      Information Units enable request                 */
   /*   1   DT_REQ      Dual Transition clocking enable request          */
   /*   2   QAS_REQ     Quick Arbitration and Selection enable request   */
   /*   3   reserved                                                     */
   /*   4   WR_FLOW     Write flow control                               */
   /*   5   RD_STRM     Read streaming                                   */
   /*   6   RTI         Retain training information                      */
   /*   7   PCOMP_EN    Precompensation enable                           */

   /* If negotiate xfer rate set to do either narrow, async, ST mode, or */
   /* the xfer period factor is not meet the DT mode requirement, then   */
   /* use ST DATA IN and ST DATA OUT phases to xfer data. */
   if ((!(xferRate[SCSI_XFER_MISC_OPT] & SCSI_WIDE)) ||
       (xferRate[SCSI_XFER_OFFSET] == 0) ||
       (!(xferRate[SCSI_XFER_PTCL_OPT] & SCSI_DT_REQ)) ||
       (hiob->SCSI_IP_workArea[3] > SCSI_MAX_DT_PERIOD_FACTOR))
   {
      hiob->SCSI_IP_workArea[7] = 0;       /* ST DATA IN and ST DATA OUT */
      if (hiob->SCSI_IP_workArea[3] < 0x0A)
      {
         hiob->SCSI_IP_workArea[3] = 0x0A; /* max. speed is 80MB/s in ST mode */
      }
   }
   else
   {
      /* Negotiation for DT, then use DT DATA IN and DT DATA OUT phases with */
      /* data group transfers, with information unit transfers, participate  */
      /* in QAS arbitrations, and/or issue QAS REQUEST messages.             */

      /* Cannot switch from Non-Packetized to Packetized mode if the current */
      /* HIOB is non-tagged command.  In that case, CHIM will negotiate for  */
      /* Non-Packetized mode (zero IU_REQ bit of PPR's protocol options).    */
#if SCSI_PACKETIZED_IO_SUPPORT
      if (!hiob->SCSI_IF_tagEnable)
      {
         /* Negotiate for non-packetized */
         hiob->SCSI_IP_workArea[7] = (SCSI_UEXACT8)
               (xferRate[SCSI_XFER_PTCL_OPT] & (SCSI_QUICKARB | SCSI_DUALEDGE));

         /* Transfer period should not be 0x08 or less in non-packetized mode */
         if (hiob->SCSI_IP_workArea[3] <= 0x08)
         {
            hiob->SCSI_IP_workArea[3] = 0x09;
         }
      }
      else
      {
         /* Negotiate for all set protocol options if 320 */
         if (xferRate[SCSI_XFER_PERIOD] <= 0x08)
         {
            /* Mask RTI if not supported by ASIC */

#if (!SCSI_ASPI_REVA_SUPPORT)
            if (SCSI_hHARPOON_REV_B(hhcb) || SCSI_hDCH_SCSI_CORE(hhcb))
            {
               hiob->SCSI_IP_workArea[7] = (SCSI_UEXACT8) (xferRate[SCSI_XFER_PTCL_OPT]);
            }
            else
#endif /* (!SCSI_ASPI_REVA_SUPPORT) */
            {
               hiob->SCSI_IP_workArea[7] = (SCSI_UEXACT8) (xferRate[SCSI_XFER_PTCL_OPT] &
                  (~SCSI_RTI));
            }
         }
         /* No pcomp, rti or hold_mcs if not 320 - check for IU */
         else if (xferRate[SCSI_XFER_PTCL_OPT] & SCSI_PACKETIZED)
         {
            hiob->SCSI_IP_workArea[7] = (SCSI_UEXACT8)
            (xferRate[SCSI_XFER_PTCL_OPT] &
               (SCSI_RD_STRM | SCSI_WR_FLOW |
                SCSI_QUICKARB |  SCSI_DUALEDGE | SCSI_PACKETIZED));
         }
         /* No rd_strm/wr_flow if not IU */
         else
         {
            hiob->SCSI_IP_workArea[7] = (SCSI_UEXACT8)
               (xferRate[SCSI_XFER_PTCL_OPT] &
                  (SCSI_QUICKARB |  SCSI_DUALEDGE | SCSI_PACKETIZED));
         }            
      }
      
#if SCSI_OEM1_SUPPORT
         /*IU only for 320 devices for OEM1*/
         if (hhcb->OEMHardware == SCSI_OEMHARDWARE_OEM1)
         {
            /*Check for global QAS disable*/
            if (hhcb->SCSI_HF_adapterQASDisable)
            {
               hiob->SCSI_IP_workArea[7] = (SCSI_UEXACT8)hiob->SCSI_IP_workArea[7] &
                 (~SCSI_QUICKARB);
            }

            if (xferRate[SCSI_XFER_PERIOD] >= 0x09)
            {
               hiob->SCSI_IP_workArea[7] = (SCSI_UEXACT8)hiob->SCSI_IP_workArea[7] &
                 (~SCSI_PACKETIZED);
            }
         }   
#endif /* SCSI_OEM1_SUPPORT */

      /* PCOMP not negotiated so regardless if negotiations successful */
      /* update current */            
      SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] |= 
                  (hiob->SCSI_IP_workArea[7] & SCSI_PCOMP_EN);
                  
      /* The latest SPI-4 specification requires that each data valid   */
      /* state REQ assertion during paced DT data phase corresponds to  */
      /* 4 bytes of data.  But Harpoon2 Rev A chip assumes only 2 bytes */
      /* per REQ assertion.  The workaround is to reduce the maximum    */
      /* offset negotiated by half and programming NEGODAT1 register    */
      /* with twice the negotiated offset.  Razor database entry #560.  */
      /* NOTE:  This has been fixed for Rev B.  Therefore we need not do*/
      /* this for Harpoon2 Rev B. */
#if SCSI_STANDARD_U320_MODE
      if (SCSI_hHARPOON_REV_A(hhcb))
      {
         if ((hiob->SCSI_IP_workArea[3] <= 0x08) &&
             (hiob->SCSI_IP_workArea[5] > (SCSI_MAX_OFFSET/2)))
         {
            hiob->SCSI_IP_workArea[5] = SCSI_MAX_OFFSET/2;
         }
      }
#endif /* SCSI_STANDARD_U320_MODE */
#else /* !SCSI_PACKETIZED_IO_SUPPORT */
      hiob->SCSI_IP_workArea[7] = (SCSI_UEXACT8)
            (xferRate[SCSI_XFER_PTCL_OPT] & (SCSI_QUICKARB | SCSI_DUALEDGE));

#endif /* SCSI_PACKETIZED_IO_SUPPORT */
      /* A problem is seen on Sony SDX-700C tape drive running 160 speed*/
      /* and offset count value greater than 0x7F.  The problem happens */
      /* on read command with one S/G element transfer and transfer     */
      /* length is greater or equal to 16k bytes.  SDX-700C tape drive  */
      /* overruns the REQ/ACK offset during the transfer, i.e. the tape */
      /* drive sends more REQ than the offset value been negotiated.    */
      /* This triggers a problem in Hapoon2 ASIC.  The problem shows up */
      /* either as command timeout where the ASIC/sequencer does not ACK*/
      /* the status byte in Rev B chip or as data corruption in both    */
      /* Rev A and Rev B chip.                                          */
      /* A workaround is implemented in CHIM to only negotiate up to    */
      /* 0x7F offset with devices in U160 speed.                        */
      if (hiob->SCSI_IP_workArea[3] == 0x09)
      {
         if (hiob->SCSI_IP_workArea[5] >= 0x80)
         {
            hiob->SCSI_IP_workArea[5] = 0x7F;
         }
      }
   }

   /* send these 8 bytes PPR extended message over to target */
   return(SCSIhSendMessage(hhcb,hiob,SCSI_PPR_MSG_LENGTH));
}
#endif /* (SCSI_PPR_ENABLE && !SCSI_ASYNC_NARROW_MODE) */

/*********************************************************************
*
*  SCSIhVerifyPPRResponse
*
*     Accept parallel protocol request response from target
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
#if (SCSI_PPR_ENABLE && !SCSI_ASYNC_NARROW_MODE)
void SCSIhVerifyPPRResponse (SCSI_HHCB SCSI_HPTR hhcb,
                             SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
   SCSI_UEXACT8 scsiID = (SCSI_UEXACT8) SCSI_TARGET_UNIT(hiob)->scsiID;
   SCSI_UEXACT8 xferRate[SCSI_MAX_XFER_PARAMETERS];
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 scsidatl;
#if SCSI_PACKETIZED_IO_SUPPORT
   SCSI_UEXACT8 pcompSave;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */  

   /* Configure hardware based on xfer period factor and offset response */
   /* from target.                                                       */
   /* Accept the value response if REQ/ACK offset is zero or xfer period */
   /* factor is not hit the limit to go async mode.                      */
   /* Otherwise, CHIM will send MESSAGE REJECT message to indicates      */
   /* narrow and async mode is the negotiated agreement.                 */

   /* If the target is able to receive the initiator's PPR values        */
   /* successfully, it returns the same or smaller values in its PPR     */
   /* message, except for the PCOMP_EN value.                            */

   /* HIOB's SCSI_IP_workArea holds the PPR message with the offset 0    */
   /* pointing at Byte 2 of the PPR message.                             */
   if ((hiob->SCSI_IP_workArea[3] == 0) ||
       (hiob->SCSI_IP_workArea[1] <= SCSI_MAX_SYNC_PERIOD_FACTOR))
   {
      /* Get nego xfer rate */
      SCSI_hGETNEGOXFERRATE(hhcb,scsiID,xferRate);

      xferRate[SCSI_XFER_PERIOD] = hiob->SCSI_IP_workArea[1];
      xferRate[SCSI_XFER_OFFSET] = hiob->SCSI_IP_workArea[3];

      /* Configure hardware based on the width response from target */
      if (hiob->SCSI_IP_workArea[4])
      {
         xferRate[SCSI_XFER_MISC_OPT] |= SCSI_WIDE;
      }
      else
      {
         xferRate[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
         deviceTable->SCSI_DF_negotiateDtc = 0; /* DT DATA xfer does not */
                                                /* run on narrow bus.    */
      }

      /* Configure hardware based on protocol options response from target. */
      /*  Bit  Definition  Meaning                                          */
      /*  ---  ----------  --------                                         */
      /*   0   IU_REQ      Information Units enable request                 */
      /*   1   DT_REQ      Dual Transition clocking enable request          */
      /*   2   QAS_REQ     Quick Arbitration and Selection enable request   */
      /*   3   reserved                                                     */
      /*   4   WR_FLOW     Write flow control                               */
      /*   5   RD_STRM     Read streaming                                   */
      /*   6   RTI         Retain training information                      */
      /*   7   PCOMP_EN    Precompensation enable                           */

      /* Must be wide and DT mode to configure protocol options */   
      if ((hiob->SCSI_IP_workArea[4]) &&
          (hiob->SCSI_IP_workArea[5] & SCSI_DT_REQ))
      {
         deviceTable->SCSI_DF_negotiateDtc = 1;

         if (hiob->SCSI_IP_workArea[5] & SCSI_PCOMP_EN)
         {
            /* */
            xferRate[SCSI_XFER_PTCL_OPT] |= SCSI_PCOMP_EN;
         }
         else
         {
            xferRate[SCSI_XFER_PTCL_OPT] &= ~SCSI_PCOMP_EN;
         }
         
         /* apply protocol options */
         /* DT transfer */
         xferRate[SCSI_XFER_PTCL_OPT] |= SCSI_DUALEDGE;

#if SCSI_PACKETIZED_IO_SUPPORT
         /* IU transfer */
         if (hiob->SCSI_IP_workArea[5] & SCSI_IU_REQ)
         {
            xferRate[SCSI_XFER_PTCL_OPT] |= SCSI_PACKETIZED;
            if (hiob->SCSI_IP_workArea[5] & SCSI_RTI)
            {
               xferRate[SCSI_XFER_PTCL_OPT] |= SCSI_RTI;
            }
            else 
            {
               xferRate[SCSI_XFER_PTCL_OPT] &= ~SCSI_RTI;
            }   
         }
         else
         {
            xferRate[SCSI_XFER_PTCL_OPT] &= 
              ~(SCSI_PCOMP_EN | SCSI_RTI | SCSI_RD_STRM |
                  SCSI_WR_FLOW | SCSI_HOLD_MCS | SCSI_PACKETIZED);
         }
         
         /* PCOMP not negotiated, save value to send determined by initiate PPR */
         pcompSave = SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] & SCSI_PCOMP_EN;
         
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

         /* QAS abritration */
         if (hiob->SCSI_IP_workArea[5] & SCSI_QAS_REQ)
         {
            xferRate[SCSI_XFER_PTCL_OPT] |= SCSI_QUICKARB;
         }
         else
         {
            xferRate[SCSI_XFER_PTCL_OPT] &= ~SCSI_QUICKARB;
         }
      }
      else
      {
         /* ST transfer mode only */
         xferRate[SCSI_XFER_PTCL_OPT] = 0;
         deviceTable->SCSI_DF_negotiateDtc = 0;

         /* Async/Narrow transfer if target's PPR response with protocol */
         /* options equal to 0 and the transfer period factor <= 0x09.   */
         if ((hiob->SCSI_IP_workArea[1] <= 0x09) &&
             ((hiob->SCSI_IP_workArea[5] &
               (SCSI_QUICKARB | SCSI_DUALEDGE | SCSI_PACKETIZED)) == 0))
         {
            xferRate[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
            xferRate[SCSI_XFER_OFFSET] = 0;
            xferRate[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
         }
      }

#if SCSI_PACKETIZED_IO_SUPPORT
      /* Before changing from Packetized to Non-Packetized and vice versa */
      /* or renegotiate from packetized mode to packetized mode,          */
      /* remember the fact that we are switching between modes.           */
      /* Since SCSI_HF_switchHappened flag was updated with the state of  */
      /* SCSI_CURRENT_XFER's packetized bit in SCSIhNegotiate routine, we */
      /* will need to update it again here only if the IU bit in target's */
      /* PPR message set.  We do this because the switch condition really */
      /* occurred only if one of the following conditions is true.        */
      /*   1) Original SCSI_HF_switchHappened = 0 and target's IU bit = 1 */
      /*   2) Original SCSI_HF_switchHappened = 1 and target's IU bit = 0 */
      /*   3) Original SCSI_HF_switchHappened = 1 and target's IU bit = 1 */
      if (hiob->SCSI_IP_workArea[5] & SCSI_IU_REQ)
      {
         hhcb->SCSI_HF_switchHappened = 1;
      }

      /* Set CURRENT and load HW negodat */
      SCSI_hXFERRATEASSIGN(hhcb,scsiID,xferRate);
      
      /* PCOMP not negotiated, restore value determined by initiate PPR */
      if (pcompSave)
      {
         SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT]  |= SCSI_PCOMP_EN;
      }
      else
      {
         SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT]  &= ~SCSI_PCOMP_EN;
      }

      if (hhcb->SCSI_HF_switchHappened)
      {
         SCSIhPackAndNonPackSwitched(hhcb,hiob);
      }
      else
      {
         /* Indicate Ack is needed for the last message byte, which is used */
         /* when CHIM acknowledges the last byte */
         hiob->SCSI_IP_ackLastMsg = SCSI_ACK_NEEDED;
      }
#else /* !SCSI_PACKETIZED_IO_SUPPORT */

      xferRate[SCSI_XFER_PTCL_OPT] &= 
         ~(SCSI_PCOMP_EN | SCSI_RTI | SCSI_RD_STRM | SCSI_WR_FLOW |
              SCSI_HOLD_MCS | SCSI_PACKETIZED);
         
      /* Update xfer rate with the current agreement */
      SCSI_hXFERRATEASSIGN(hhcb,scsiID,xferRate);

      /* Indicate Ack is needed for the last message byte, which is used */
      /* when CHIM acknowledges the last byte */
      hiob->SCSI_IP_ackLastMsg = SCSI_ACK_NEEDED;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
   }
   else
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE | SCSI_ATNO);
      do {
         scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
         phase = SCSIhWait4Req(hhcb);
      } while (phase == SCSI_MIPHASE);
      if (phase != SCSI_MOPHASE)
      {
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID19);
         return;
      }
      else
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MOPHASE);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL), SCSI_MSG07);
         if (SCSIhWait4Req(hhcb) == (SCSI_UEXACT8)-1)
         {
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID20);
            return;
         }
         /* set reentrant address for sequencer */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                       (SCSI_UEXACT8)(SCSI_hSIOSTR3_ENTRY(hhcb) >> 2));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                       (SCSI_UEXACT8)(SCSI_hSIOSTR3_ENTRY(hhcb) >> 10));
      }
   }

   /* set up SCSI_LAST_NEGO_XFER entry in case for subsequent negotiation */
   SCSI_hLASTNEGOENTRYASSIGN(hhcb,deviceTable);
   
   /* done with negotiation state */
   hiob->SCSI_IP_negoState = SCSI_NOT_NEGOTIATING;
}
#endif /* (SCSI_PPR_ENABLE && !SCSI_ASYNC_NARROW_MODE) */

#if !SCSI_ASYNC_NARROW_MODE
/*********************************************************************
*
*  SCSIhInitiateWide
*
*     Initiate wide negotiation process
*
*  Return Value:  -1 if SCSIhBadSeq was called for error 
*                 recovery 
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*                 Parameter hiob will be posted to OSM if
*                 SCSIhBadSeq was called for error handling.
*                 Therefore, hiob should not be referenced
*                 on return.
*
*********************************************************************/
SCSI_UEXACT8 SCSIhInitiateWide (SCSI_HHCB SCSI_HPTR hhcb,
                                SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_UEXACT8 scsiID = (SCSI_UEXACT8) SCSI_TARGET_UNIT(hiob)->scsiID;
   SCSI_UEXACT8 xferRate[SCSI_MAX_XFER_PARAMETERS];

   /* Indicate initiate WDTR nego state */
   hiob->SCSI_IP_negoState = SCSI_INITIATE_WIDE;

   /* Get nego xfer rate */
   SCSI_hGETNEGOXFERRATE(hhcb,scsiID,xferRate);

   /* Build wide negotiation message based on configuration */
   hiob->SCSI_IP_workArea[0] = SCSI_MSG01;
   hiob->SCSI_IP_workArea[1] = 2;
   hiob->SCSI_IP_workArea[2] = SCSI_MSGWIDE;

   /* setup transfer width */
   hiob->SCSI_IP_workArea[3] =
                     (SCSI_UEXACT8) (xferRate[SCSI_XFER_MISC_OPT] & SCSI_WIDE);

   /* send these 4 bytes WDTR extended message over to target */
   return(SCSIhSendMessage(hhcb,hiob,SCSI_WDTR_MSG_LENGTH));
}
#endif /* !SCSI_ASYNC_NARROW_MODE */

#if !SCSI_ASYNC_NARROW_MODE
/*********************************************************************
*
*  SCSIhVerifyWideResponse
*
*     Accept wide response from target
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
void SCSIhVerifyWideResponse (SCSI_HHCB SCSI_HPTR hhcb,
                              SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
   SCSI_UEXACT8 scsiID = (SCSI_UEXACT8) SCSI_TARGET_UNIT(hiob)->scsiID;
   SCSI_UEXACT8 scsidatl;
   SCSI_UEXACT8 xferRate[SCSI_MAX_XFER_PARAMETERS];

   /* Get nego xfer rate */
   SCSI_hGETNEGOXFERRATE(hhcb,scsiID,xferRate);

   /* Set reentrant address for sequencer */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                 (SCSI_UEXACT8) (SCSI_hSIOSTR3_ENTRY(hhcb) >> 2));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                 (SCSI_UEXACT8) (SCSI_hSIOSTR3_ENTRY(hhcb) >> 10));

   /* Configure hardware based on wide response from target. */
   /* HIOB's SCSI_IP_workArea holds the WDTR message with    */
   /* the offset 0 pointing at Byte 2 of the WDTR message.   */
   if (hiob->SCSI_IP_workArea[1])
   {
      xferRate[SCSI_XFER_MISC_OPT] |= SCSI_WIDE;
   }
   else
   {
      xferRate[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
   }

   /* Update the current negotiated rates entry and ASIC rate registers */
   SCSI_hXFERRATEASSIGN(hhcb,scsiID,xferRate);

   SCSI_hSETNEGOTIATEDSTATE(hhcb,scsiID,1);

   if (SCSI_hSYNCNEGONEEDED(deviceTable))
   {
      /* Assert ATN and ack the last msg byte received */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE | SCSI_ATNO);
      scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));

      /* Match the phase */                              
      if (SCSIhWait4Req(hhcb) == (SCSI_UEXACT8)-1)
      {
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID21);
         return;
      }

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO),
            OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_BUSPHASE);

      /* continue with initiating synchronous negotiation procedure */
      SCSIhInitiateSync(hhcb,hiob);
   }
   else
   {
      /* Ack the last msg byte received. */
      /* Handle the case where a device is wide but does not */
      /* support sync. or sync. disabled via target profile. */
      scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));

      /* set up SCSI_LAST_NEGO_XFER entry in case for subsequent negotiation */
      SCSI_hLASTNEGOENTRYASSIGN(hhcb,deviceTable);

      /* done with negotiation */
      hiob->SCSI_IP_negoState = SCSI_NOT_NEGOTIATING;
   }
}
#endif /* !SCSI_ASYNC_NARROW_MODE */

#if !SCSI_ASYNC_NARROW_MODE
/*********************************************************************
*
*  SCSIhInitiateSync
*
*     Initiate synchronous negotiation process
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*                 Assume we are in Mode 3.
*
*********************************************************************/
void SCSIhInitiateSync (SCSI_HHCB SCSI_HPTR hhcb,
                        SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 scsiID = (SCSI_UEXACT8) SCSI_TARGET_UNIT(hiob)->scsiID;
   SCSI_UEXACT8 xferRate[SCSI_MAX_XFER_PARAMETERS];
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 scsidatl;

   /* Indicate initiate SDTR nego state */
   hiob->SCSI_IP_negoState = SCSI_INITIATE_SYNC;

   /* Get nego xfer rate */
   SCSI_hGETNEGOXFERRATE(hhcb,scsiID,xferRate);

   /* build synchronous negotiation message */   
   hiob->SCSI_IP_workArea[0] = SCSI_MSG01;
   hiob->SCSI_IP_workArea[1] = 3;
   hiob->SCSI_IP_workArea[2] = SCSI_MSGSYNC;

   /* setup transfer period */
   hiob->SCSI_IP_workArea[3] = xferRate[SCSI_XFER_PERIOD];

   /* setup REQ/ACK offset */
   hiob->SCSI_IP_workArea[4] = xferRate[SCSI_XFER_OFFSET];

   /* send these 5 bytes SDTR extended message over to target */
   if (SCSIhSendMessage(hhcb,hiob,SCSI_SDTR_MSG_LENGTH) == ((SCSI_UEXACT8)-1))
   {
      /* SCSIhBadSeq was called so just terminate */
      return;
   }
   
   /* Handle message reject */
   if ((phase = SCSIhWait4Req(hhcb)) == SCSI_MIPHASE)
   {
      /* if target responds with message reject */
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIBUSL)) == SCSI_MSG07)
      {
         /* match the phase and ack the last msg byte received */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE);
         scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));

         /* match the phase */                           
         if (SCSIhWait4Req(hhcb) == (SCSI_UEXACT8)-1)
         {
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID22);
            return;
         }

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO),
               OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_BUSPHASE);
      }
   }
   else if (phase == (SCSI_UEXACT8)-1)
   {
      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID23);
      return;
   }
}
#endif /* !ASYNC_NARROW_MODE */

#if !SCSI_ASYNC_NARROW_MODE
/*********************************************************************
*
*  SCSIhVerifySyncResponse
*
*     Accept sync response from target
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
void SCSIhVerifySyncResponse (SCSI_HHCB SCSI_HPTR hhcb,
                              SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
   SCSI_UEXACT8 scsiID = (SCSI_UEXACT8) SCSI_TARGET_UNIT(hiob)->scsiID;
   SCSI_UEXACT8 xferRate[SCSI_MAX_XFER_PARAMETERS];
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 scsidatl;

   /* Configure hardware based on xfer period factor and offset */
   /* response from target.                                     */
   /* Accept the value response if REQ/ACK offset is zero or    */
   /* xfer period factor is not hit the limit to go async mode. */
   /* Otherwise, CHIM will send MESSAGE REJECT message to       */
   /* indicates async mode is the negotiated agreement.         */

   /* HIOB's SCSI_IP_workArea holds the SDTR message with the   */
   /* offset 0 pointing at Byte 2 of the SDTR message.          */
   if ((hiob->SCSI_IP_workArea[2] == 0) ||
       (hiob->SCSI_IP_workArea[1] <= SCSI_MAX_SYNC_PERIOD_FACTOR))
   {
      /* Get nego xfer rate */
      SCSI_hGETNEGOXFERRATE(hhcb,scsiID,xferRate);

      xferRate[SCSI_XFER_PERIOD] = hiob->SCSI_IP_workArea[1];
      xferRate[SCSI_XFER_OFFSET] = hiob->SCSI_IP_workArea[2];

      /* If not currently running wide transfers then turn off the
       * wide bit.
       */
      if (!(SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_MISC_OPT] & SCSI_WIDE))
      {
         xferRate[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
      }

#if SCSI_NEGOTIATION_PER_IOB
      if (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_MISC_OPT] & SCSI_WIDE)
      {
         xferRate[SCSI_XFER_MISC_OPT] |= SCSI_WIDE;
      }
#endif /* SCSI_NEGOTIATION_PER_IOB */

      /* Update xfer rate with the current agreement */
      SCSI_hXFERRATEASSIGN(hhcb,scsiID,xferRate);

      SCSI_hSETNEGOTIATEDSTATE(hhcb,scsiID,1);

      /* Indicate Ack is needed for the last message byte, which is used */
      /* when CHIM acknowledges the last byte */
      hiob->SCSI_IP_ackLastMsg = SCSI_ACK_NEEDED;
   }
   else
   
   {
   
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE | SCSI_ATNO);
      do {
         scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
         phase = SCSIhWait4Req(hhcb);
      } while (phase == SCSI_MIPHASE);
      if (phase != SCSI_MOPHASE)
      {
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID24);
         return;
      }
      else
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MOPHASE);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL), SCSI_MSG07);
         if (SCSIhWait4Req(hhcb) == (SCSI_UEXACT8)-1)
         {
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID25);
            return;
         }
         /* Set reentrant address for sequencer */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                       (SCSI_UEXACT8)(SCSI_hSIOSTR3_ENTRY(hhcb) >> 2));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                       (SCSI_UEXACT8)(SCSI_hSIOSTR3_ENTRY(hhcb) >> 10));
      }
   }

   /* set up SCSI_LAST_NEGO_XFER entry in case for subsequent negotiation */
   SCSI_hLASTNEGOENTRYASSIGN(hhcb,deviceTable);

   /* Done with negotiation state */
   hiob->SCSI_IP_negoState = SCSI_NOT_NEGOTIATING;
}
#endif /* !SCSI_ASYNC_NARROW_MODE */

/*********************************************************************
*
*  SCSIhRespondToWide
*
*     Respond to wide negotiation initiated from target
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       CHIM will response to a target wide negotiation
*                 based on the scsiOption's wide_xfer bit.  CHIM will
*                 response to run in narrow mode if the current
*                 negotiation was suppressed.
*
*********************************************************************/
void SCSIhRespondToWide (SCSI_HHCB SCSI_HPTR hhcb,
                         SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
#if SCSI_ASYNC_NARROW_MODE
   SCSI_DEVICE SCSI_DPTR deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
#endif /* SCSI_ASYNC_NARROW_MODE */
   SCSI_UEXACT8 scsiID = (SCSI_UEXACT8) SCSI_TARGET_UNIT(hiob)->scsiID;
   SCSI_UEXACT8 xferRate[SCSI_MAX_XFER_PARAMETERS];

#if SCSI_ASYNC_NARROW_MODE

#if SCSI_BIOS_SUPPORT
   if (!hhcb->SCSI_HF_biosRunTimeEnabled)
#endif /* SCSI_BIOS_SUPPORT */
   {
      /* Clear initiate xferRate's wide bit */
      SCSI_NEGO_XFER(deviceTable)[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
   }

   xferRate[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
   xferRate[SCSI_XFER_OFFSET] = 0;
   xferRate[SCSI_XFER_PTCL_OPT] = 0;   /* zero protocol options */
   xferRate[SCSI_XFER_MISC_OPT] = 0;   /* zero wide bit */

#else /* !SCSI_ASYNC_NARROW_MODE */

   /* Get nego xfer Rate */
   SCSI_hGETNEGOXFERRATE(hhcb,scsiID,xferRate);

   /* Zero protocol options field and offset because it is WDTR negotiation. */
   xferRate[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
   xferRate[SCSI_XFER_OFFSET] = 0;
   xferRate[SCSI_XFER_PTCL_OPT] = 0;   /* zero protocol options */

   /* Coordinate with whatever target initiated.  Match the wide transfer. */
   /* HIOB's SCSI_IP_workArea holds the WDTR message with the offset 0     */
   /* pointing at Byte 2 of the PPR message.                               */
   if (hiob->SCSI_IP_workArea[1] < (xferRate[SCSI_XFER_MISC_OPT] & SCSI_WIDE))
   {
      /* Turn off wide. */
      xferRate[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
   }

#endif /* SCSI_ASYNC_NARROW_MODE */

   SCSI_hSETNEGOTIATEDSTATE(hhcb,scsiID,1);

   /* Update xfer rate with the current agreement */
   SCSI_hXFERRATEASSIGN(hhcb,scsiID,xferRate);

   /* build wide negotiation message */
   hiob->SCSI_IP_workArea[0] = SCSI_MSG01;
   hiob->SCSI_IP_workArea[1] = 2;
   hiob->SCSI_IP_workArea[2] = SCSI_MSGWIDE;
   hiob->SCSI_IP_workArea[3] = (SCSI_UEXACT8)(xferRate[SCSI_XFER_MISC_OPT] & SCSI_WIDE);

   /* assert ATN, message will be sent at: */
   /* 1. negotiate routine if target initiated wide nego. right before */
   /*    command phase phase. */
   /* 2. handle message out routine if the target initiated wide nego. */
   /*    before the data phase. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE | SCSI_ATNO);

   hiob->SCSI_IP_negoState = SCSI_RESPONSE_WIDE;

   /* Indicate Ack is needed for the last message byte, which is used */
   /* when CHIM acknowledges the last byte */
   hiob->SCSI_IP_ackLastMsg = SCSI_ACK_NEEDED;
}

/*********************************************************************
*
*  SCSIhRespondToSync
*
*     Respond to sync negotiation initiated from target
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:       CHIM will response to a target sync negotiation
*                 based on the xferRate's offset and period.
*                 CHIM will response to run in async mode if
*                 the current negotiation was suppressed.
*
*********************************************************************/
void SCSIhRespondToSync (SCSI_HHCB SCSI_HPTR hhcb,
                         SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
   SCSI_UEXACT8 scsiID = (SCSI_UEXACT8) SCSI_TARGET_UNIT(hiob)->scsiID;
   SCSI_UEXACT8 xferRate[SCSI_MAX_XFER_PARAMETERS];

#if SCSI_ASYNC_NARROW_MODE

#if SCSI_BIOS_SUPPORT
   if (!hhcb->SCSI_HF_biosRunTimeEnabled)
#endif /* SCSI_BIOS_SUPPORT */
   {
      /* Clear initiate scsi offset */
      SCSI_NEGO_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;
   }

   xferRate[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
   xferRate[SCSI_XFER_OFFSET] = 0;
   xferRate[SCSI_XFER_PTCL_OPT] = 0;   /* zero protocol options */
   xferRate[SCSI_XFER_MISC_OPT] = 0;   /* zero wide bit */

#else /* !SCSI_ASYNC_NARROW_MODE */

   /* Get nego xfer rate */
   SCSI_hGETNEGOXFERRATE(hhcb,scsiID,xferRate);

   /* Zero protocol options field because it is a SDTR negotiation. */
   /* We must be running ST mode.                                   */ 
   xferRate[SCSI_XFER_PTCL_OPT] = 0;

   /* Either negotiation ever took place or not, the current xfer entry will */
   /* have the valid wide configuration.  So, get the current wide config.   */
   xferRate[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
   xferRate[SCSI_XFER_MISC_OPT] |= (SCSI_UEXACT8)
            (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_MISC_OPT] & SCSI_WIDE);

   /* HIOB's SCSI_IP_workArea holds the WDTR message with the offset 0 */
   /* pointing at Byte 2 of the PPR message.                           */
   /* Match the scsi period and offset.                                */
   /* If the offset is non-zero, target will do synchronous xfer.      */
   if (hiob->SCSI_IP_workArea[2])
   {                                
      /* Take the smaller offset of the two */
      if (hiob->SCSI_IP_workArea[2] < xferRate[SCSI_XFER_OFFSET])
      {
         xferRate[SCSI_XFER_OFFSET] = hiob->SCSI_IP_workArea[2];
      }

      /* If target's xfer period is slower than the supported synchronous */
      /* xfer period, then set to run asynchronous xfer. */
      if (hiob->SCSI_IP_workArea[1] > SCSI_MAX_SYNC_PERIOD_FACTOR)
      {
         xferRate[SCSI_XFER_OFFSET] = 0;
      }
      else if (hiob->SCSI_IP_workArea[1] > xferRate[SCSI_XFER_PERIOD])
      {
         /* The smaller xfer period is the faster. */
         /* Take the slower speed of the two */
         xferRate[SCSI_XFER_PERIOD] = hiob->SCSI_IP_workArea[1];
      }
   }
   else
   {
      xferRate[SCSI_XFER_OFFSET] = 0;
   }

#endif /* SCSI_ASYNC_NARROW_MODE */

   SCSI_hSETNEGOTIATEDSTATE(hhcb,scsiID,1);

   /* Update xfer rate with the current agreement */
   SCSI_hXFERRATEASSIGN(hhcb,scsiID,xferRate);

   /* build response messages */
   hiob->SCSI_IP_workArea[0] = SCSI_MSG01;
   hiob->SCSI_IP_workArea[1] = 3;
   hiob->SCSI_IP_workArea[2] = SCSI_MSGSYNC;
   hiob->SCSI_IP_workArea[3] = xferRate[SCSI_XFER_PERIOD];
   hiob->SCSI_IP_workArea[4] = xferRate[SCSI_XFER_OFFSET];

   /* assert ATN, message will be sent at: */
   /* 1. negotiate routine if target initiated sync nego. right before */
   /*    command phase phase. */
   /* 2. handle message out routine if the target initiated sync nego. */
   /*    before the data phase. */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE | SCSI_ATNO);

   /* If current SCSI_IP_negoState is SCSI_RESPONSE_MSG07_WIDE,        */
   /* change to SCSI_RESPONSE_MSG07_SYNC state.                        */
   if (hiob->SCSI_IP_negoState == SCSI_RESPONSE_MSG07_WIDE)
   {
      hiob->SCSI_IP_negoState = SCSI_RESPONSE_MSG07_SYNC;
   }
   else
   {
      hiob->SCSI_IP_negoState = SCSI_RESPONSE_SYNC;
   }

   /* Indicate Ack is needed for the last message byte, which is used */
   /* when CHIM acknowledges the last byte */
   hiob->SCSI_IP_ackLastMsg = SCSI_ACK_NEEDED;
}

/*********************************************************************
*
*  SCSIhSendMessage
*
*     Send message out to target
*
*  Return Value:  -1 if SCSIhBadSeq was called for
*                 error recovery. Note
*                 the hiob must not be accessed as
*                 it has been returned to the caller
*                  
*  Parameters:    hhcb
*                 hiob
*                 count of message to be sent
*
*  Remarks:
*
*                 Assume we are in Mode 3.
*
*********************************************************************/
SCSI_UEXACT8 SCSIhSendMessage (SCSI_HHCB SCSI_HPTR hhcb,
                               SCSI_HIOB SCSI_IPTR hiob,
                               int count)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UINT32 j = 0;
   SCSI_UEXACT8 result = 0;
   SCSI_UEXACT8 phase;
   int i;

   /* we must be in message out phase to send message */
   if (SCSIhWait4Req(hhcb) != SCSI_MOPHASE)
   {
      return(result);
   }

   /* Transfer all but the last byte of the extended message */
   for (i = 0; i < count-1; i++)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL), hiob->SCSI_IP_workArea[i]);
      if ((phase = SCSIhWait4Req(hhcb)) != SCSI_MOPHASE)
      {
         if (phase == (SCSI_UEXACT8)-1)
         {
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID26);
            return(phase);
         }

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);
      
         return(result);
      }
   }

   /* Here we may have to handle a reject message from a target during  
    * Select with Attn. The BIOS, ASPI and or CHIM  drivers can begin 
    * there initial bus scanning without issuing a Scsi Bus reset 
    * therefore the Target's previously negoitated state would be unknown.  
    * In accordance with SPI4 (rev 6), if a target is selected with ATTN  
    * while currently negotiated for Packetized operation it shall reject 
    * any message other than a PPR or Task Mgt it receives. The target will 
    * then initiate a WDTR message with WIDE disabled essentially dropping 
    * out of Packetized mode. A new negoState was added to assist with this 
    * recovery. Here we check for that flag allowing the process to pass it
    * in to the an existing path that prepares the Sequencer to expect a bus 
    * free upon completion of the WDTR message. The SCB is the re-queued 
    * for a 2nd attempt. 
    */

#if (!SCSI_ASYNC_NARROW_MODE)
   if ((hiob->SCSI_IP_negoState == SCSI_RESPONSE_MSG07_WIDE) ||
       (hiob->SCSI_IP_negoState == SCSI_RESPONSE_MSG07_SYNC)) 
   {
      return((SCSI_UEXACT8)SCSIhPackSwitchedMsgReject(hhcb,hiob));
   }
#endif /* (!SCSI_ASYNC_NARROW_MODE && SCSI_PACKETIZED_IO_SUPPORT) */

   /* deassserted ATN and transfer the last byte */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL), hiob->SCSI_IP_workArea[i]);

   /* wait until ACK get dropped */
   while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_ACKI) &&
          (j++ < SCSI_REGISTER_TIMEOUT_COUNTER))
   {
      OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
   }

   return(result);
}

/*********************************************************************
*
*  SCSIhReceiveMessage
*
*     Receive message from target
*
*  Return Value:  number of bytes actually received
*                 if -1 then indicates SCSIhBadSeq
*                 was called. 
*                  
*  Parameters:    hhcb
*                 hiob
*                 count of message to be received
*
*  Remarks:
*
*                 Assume we are in Mode 3.
*
*********************************************************************/
SCSI_UEXACT8 SCSIhReceiveMessage (SCSI_HHCB SCSI_HPTR hhcb,
                                  SCSI_HIOB SCSI_IPTR hiob,
                                  SCSI_UEXACT8 count)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 sxfrctl0;
   SCSI_UEXACT8 sxfrctl1;
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 i;
   SCSI_UEXACT8 scsidatl;

   for (i = 0; i < count; i++)
   {
      if ((phase = SCSIhWait4Req(hhcb)) != SCSI_MIPHASE)
      {
         if (phase == (SCSI_UEXACT8)-1)
         {
            SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID27);
            i = phase;
            break;
         }

         if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) &
             (SCSI_BUSPHASE | SCSI_ATNI)) == (SCSI_MOPHASE | SCSI_ATNI))
         {
            /* must be parity error, clear it */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MOPHASE);
            sxfrctl0 = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL0));
            sxfrctl1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), sxfrctl1 & ~SCSI_ENSPCHK);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), sxfrctl1 | SCSI_ENSPCHK);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), SCSI_CLRSCSIINT);

            /* Place message parity error on bus without an ack */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL0), sxfrctl0 & ~SCSI_SPIOEN);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL), SCSI_MSG09);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL0), sxfrctl0 | SCSI_SPIOEN);
         }

         break;
      }
      else
      {
         /* collect the message bytes */
         hiob->SCSI_IP_workArea[i] = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIBUSL));

         /* ack the byte except the last one so that we can assert */
         /* ATN just in case */
         if (i != (count - 1))
         {
            scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
         }
      }
   }

   return(i);
}

/*********************************************************************
*
*  SCSIhHandleMsgOut
*
*     Handle message out interrupt from sequencer
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
void SCSIhHandleMsgOut (SCSI_HHCB SCSI_HPTR hhcb,
                        SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
   SCSI_UEXACT8 sxfrctl1;
   SCSI_UINT32 count = (SCSI_UINT32)0x800000;

   /* match meessage out phase */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MOPHASE);

   if (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_ATNI)
   {
      switch(hiob->SCSI_IP_negoState)
      {
#if SCSI_PPR_ENABLE
         case SCSI_RESPONSE_PPR:
            /* send 8-bytes PPR extended message response to target */
            /* xfer rate already updated during handling extended msg in. */
            if (SCSIhSendMessage(hhcb,hiob,SCSI_PPR_MSG_LENGTH) == ((SCSI_UEXACT8)-1))
            {
               /* SCSIhBadSeq was called so just terminate */
               return;
            }

            /* set up SCSI_LAST_NEGO_XFER entry in case */
            /* for subsequent negotiation */
            SCSI_hLASTNEGOENTRYASSIGN(hhcb,deviceTable);

            /* Done with negotiation state */
            hiob->SCSI_IP_negoState = SCSI_NOT_NEGOTIATING;
            break;
#endif /* SCSI_PPR_ENABLE */

         case SCSI_RESPONSE_WIDE:
            /* send 4-bytes WDTR extended message response to target */
            /* xfer rate already updated during handling extended msg in. */
            if (SCSIhSendMessage(hhcb,hiob,SCSI_WDTR_MSG_LENGTH) == ((SCSI_UEXACT8)-1))
            {
               /* SCSIhBadSeq was called so just terminate */
               return;
            }

            /* set up SCSI_LAST_NEGO_XFER entry in case */
            /* for subsequent negotiation */
            SCSI_hLASTNEGOENTRYASSIGN(hhcb,deviceTable);

            /* Done with negotiation state */
            hiob->SCSI_IP_negoState = SCSI_NOT_NEGOTIATING;
            break;
      
         case SCSI_RESPONSE_SYNC:
            /* send 5-bytes SDTR extended message response to target */
            /* xfer rate already updated during handling extended msg in. */
            if (SCSIhSendMessage(hhcb,hiob,SCSI_SDTR_MSG_LENGTH) == ((SCSI_UEXACT8)-1))
            {
               /* SCSIhBadSeq was called so just terminate */
               return;
            }

            /* set up SCSI_LAST_NEGO_XFER entry in case */
            /* for subsequent negotiation */
            SCSI_hLASTNEGOENTRYASSIGN(hhcb,deviceTable);

            /* Done with negotiation state */
            hiob->SCSI_IP_negoState = SCSI_NOT_NEGOTIATING;
            break;

         default:
            /* must be parity or CRC error etc */
            sxfrctl1 = OSD_INEXACT8(SCSI_AICREG(SCSI_SXFRCTL1));
            
            /* Turn off parity checking to clear any residual error. */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), sxfrctl1 & ~SCSI_ENSPCHK);

            /* Turn it back on explicitly because it may have been cleared */
            /* in 'SCSIhPhysicalError'.  (It had to been previously set or */
            /* we wouldn't have gotten here.)                              */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SXFRCTL1), sxfrctl1 | SCSI_ENSPCHK);

            /* Parity and CRC checking are two separated control bits: */
            /* ENSPCHK and CRCVALCHKEN respectively.                   */
            /* Enable CRC checking.                                    */
            SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE4);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CRCCONTROL),
               (SCSI_UEXACT8)(OSD_INEXACT8(SCSI_AICREG(SCSI_CRCCONTROL)) |
                              SCSI_CRCVALCHKEN));
            SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);

            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1),
                 SCSI_CLRSCSIPERR | SCSI_CLRATNO);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT), SCSI_CLRSCSIINT);

            /* Send message to target */
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL),
                 hiob->SCSI_IP_workArea[SCSI_MAX_WORKAREA-1]);
            break;
      }
   }
   else
   {
      /* just ignore it */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL), SCSI_MSG08);
   }

   while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_ACKI) && (--count))
      ;
}

/*********************************************************************
*
*  SCSIhHandleMsgIn
*
*     Handle message unknown to sequencer
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*                 Assume we are in Mode 3.
*
*********************************************************************/
void SCSIhHandleMsgIn (SCSI_HHCB SCSI_HPTR hhcb,
                       SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 rejectedMsg;
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 ignoreXfer;
   SCSI_UEXACT8 message;
   SCSI_UEXACT8 scsidatl;
   SCSI_UINT32 j;       /* time out counter */

/****************************************************************
 * Added check for unexpected MSGIN condition when a target 
 * rejects the Identify message while in Packetize mode. This 
 * complies with the SPI4 protocol.
 ****************************************************************/     
 
   /* Get last msg sent */
   rejectedMsg = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL_IMG));
   
   if (( (message = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIBUSL))) == SCSI_MSG07) &&
       (rejectedMsg & SCSI_MSGID))

   {
      
      /* clear attn out */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);

      /* ACK MSG07 message byte */
      scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
   
      /*This may be a target protocol error, look at next phase.*/
      phase = SCSIhWait4Req(hhcb);
      
      /*Handle bus free after message reject of the Identify message.*/
      if ((OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1))) & (SCSI_BUSFREE | SCSI_SCSIRSTI))
      {
         /*Restart sequencer at idle if bus free protocol error.*/
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                    (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                    (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));
      }
      else if ((SCSIhReceiveMessage(hhcb,hiob,1)) == ((SCSI_UEXACT8)1))
      {
         /* if target asserts REQ and places a MSG01 on the bus, it is     */
         /* following the SPI4 protocol for handling a message other than  */
         /* PPR or Target Reset if currently negotiated for PACKETIZED.    */

         /* read without ACK */         
         if (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIBUSL)) == SCSI_MSG01)
         {      
            /* ACK extdmsg message byte */
            scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));

            /* get next msg byte (length) */            
            ignoreXfer = SCSIhReceiveMessage(hhcb,hiob,1);
            
            /* ACK "number bytes" message byte */
            scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
            
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_hPASS_TO_DRIVER(hhcb)), 
               hiob->SCSI_IP_workArea[0]);   /* put number of bytes into pass to driver */
                                             /* assume ack is not needed */
            hiob->SCSI_IP_ackLastMsg = SCSI_ACK_NOT_NEEDED;
                                             /* and call SCSIhExtMsgi */
            SCSIhExtMsgi(hhcb,hiob);
            
            if (hiob->SCSI_IP_ackLastMsg == SCSI_ACK_NEEDED)
            {
               /* ACK last message byte */
               scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
            }
            
            /* set this flag which guides the code through the appropriate */
            /* code paths to handle when target responds WDTR and SDTR     */
            /* after message reject to get out of packetized mode.        */
            hiob->SCSI_IP_negoState = SCSI_RESPONSE_MSG07_WIDE;
            return;
         }    
      }
        
      /* The target either failed to assert REQ or */
      /* the msg it sent was not a WDTR or SDTR.   */
      SCSI_hTARGETCLEARBUSY(hhcb,hiob);

      /* Wait for the bus to go to BUS FREE phase. */
      j = 0;
      while (j++ < SCSI_REGISTER_TIMEOUT_COUNTER)
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

      /*If we haven't gone bus free try to force abort*/      
      if (j >= SCSI_REGISTER_TIMEOUT_COUNTER)
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE | SCSI_ATNO);
         scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
         SCSIhAbortConnection(hhcb,hiob,(SCSI_UEXACT8)SCSI_MSG06);
      }   
      
      if (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & SCSI_BUSFREE)
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRBUSFREE);
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRSCSIINT);
      }

      hiob->haStat = SCSI_HOST_PROTOCOL_ERROR;
      SCSIhTerminateCommand(hiob);
      return;
   }
   
   if (((OSD_INEXACT8(SCSI_AICREG(SCSI_SCSISIGI)) & SCSI_ATNI) == 0) && (message != 0x03))
   {
      /* reading without ACK */
      switch (OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIBUSL)))
      {
         case SCSI_MSG01:
            /* ACK MSG01 message byte */
            scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
            /* collect extended messages count byte */
            if ((SCSIhReceiveMessage(hhcb,hiob,1)) != ((SCSI_UEXACT8)1))
            {
               /* something wrong */
               return;
            }
            /* ACK "number bytes" message byte */
            scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
            OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_hPASS_TO_DRIVER(hhcb)), 
               hiob->SCSI_IP_workArea[0]);   /* put number of bytes into pass to driver */
                                             /* assume ack is not needed */
            hiob->SCSI_IP_ackLastMsg = SCSI_ACK_NOT_NEEDED;
                                             /* and call SCSIhExtMsgi */
            SCSIhExtMsgi(hhcb,hiob);
            if (hiob->SCSI_IP_ackLastMsg == SCSI_ACK_NEEDED)
            {
               /* ACK last message byte */
               scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
            }
            return;

         case SCSI_MSG07:
            /* If msg Identify or tag type, abort */
            if (rejectedMsg & (SCSI_MSGID | SCSI_MSGTAG))
            {
               OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE | SCSI_ATNO);
               scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));

               SCSIhAbortConnection(hhcb,hiob,(SCSI_UEXACT8)SCSI_MSG06);

               SCSI_hTARGETCLEARBUSY(hhcb,hiob);

               hiob->haStat = SCSI_HOST_PROTOCOL_ERROR;
               SCSIhTerminateCommand(hiob);

               return;
            }
            break;

         case SCSI_MSGIWR:                      /* ignore wide residue */
            /* To handle the IGNORE WIDE RESIDUE message:
               1. ACK the 0x23h message.
               2. read w/o ACK the 2nd message byte.
               3. Increment STCNT0-3 to back up due to the extra bad byte(s).
               4. Increment rescnt field of SCB by the reduced number.
               5. Read SHADDR0-3 registers, decrement by the reduced number,
                  and write to HADDR0-3 which will shine thru to SHADDR0-3.
               6. ACK the 2nd message byte.(Done outside of the switch).
               7. Unpause the sequencer.(Done by PH_IntHandler() when return).
            */
            /* ACK MSG23 message byte */
            scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
            if ((phase = SCSIhWait4Req(hhcb)) == (SCSI_UEXACT8)-1)
            {
               SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID28);
               return;
            }
            /* read 2nd byte, no ACK */
            ignoreXfer = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIBUSL));
            if (ignoreXfer)
            {
               /* do nothing if zero */
               SCSI_hIGNOREWIDERESIDUECALC(hhcb,hiob,ignoreXfer);
            }
            break;


         default:
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), (SCSI_MIPHASE | SCSI_ATNO));
            do
            {
               scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
               phase = SCSIhWait4Req(hhcb);
            } while (phase == SCSI_MIPHASE);
            if (phase != SCSI_MOPHASE)
            {
               SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID29);
               return;
            }
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MOPHASE);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);
            OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL), SCSI_MSG07);
            return;
      }                          /* end of switch statement */
   }

   /* Drive ACK active to release SCSI bus */
   scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
}

/*********************************************************************
*
*  SCSIhAbortConnection
*
*     Generate the appropriate abort message on the SCSI bus. 
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*                 abortMsg - message to be output,
*                            SCSI_MSG06 or SCSI_MSG0D
*
*  Remarks:
*                 This routine is called to abort a request that
*                 is currently connected on the SCSI bus.
*                 This routine performs the abort and expects
*                 the target to go to bus free. If the target
*                 doesn't go bus free or some other unexpected
*                 condition occurs, routine SCSIhBadSeq is called
*                 to clean up.
*
*  Assumptions:   hiob valid
*                 Caller asserted ATN and expect target to assert REQ
*                 in message out phase.
*
*********************************************************************/
void SCSIhAbortConnection (SCSI_HHCB SCSI_HPTR hhcb,
                           SCSI_HIOB SCSI_IPTR hiob,
                           SCSI_UEXACT8 abortMsg)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UINT32 count;        /* time out counter */

   /* Target should be in message-out phase - if not call
    * SCSIhBadSeq.
    */
   if (SCSIhWait4Req(hhcb) == SCSI_MOPHASE)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MOPHASE);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);

      /* Output the message */ 
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL), abortMsg);

      count = 0;
      while ((!(OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & 
               (SCSI_BUSFREE | SCSI_SCSIRSTI))) &&
             (count++ < SCSI_REGISTER_TIMEOUT_COUNTER))
      {
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      }

      /* Expecting the target has gone to Bus Free */
      if ((OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT)) & SCSI_SCSIINT) &&
          (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & SCSI_BUSFREE))
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRBUSFREE);

         /* Restart sequencer in idle loop */
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                       (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                       (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRSCSIINT);
       }
       else
       {
          SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID61);
       }
   }
   else
   {
      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID62);
   }

   return;
}

/*********************************************************************
*
*  SCSIhPackAndNonPackSwitched
*
*     Handle mode switch between Packetized and Non-Packetized
*
*  Return Value:   0 - successful
*                 -1 - if SCSIhBadSeq was called for error recovery.
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/

#if (!SCSI_ASYNC_NARROW_MODE && SCSI_PACKETIZED_IO_SUPPORT)
SCSI_UEXACT8 SCSIhPackAndNonPackSwitched (SCSI_HHCB SCSI_HPTR hhcb,
                                          SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = SCSI_TARGET_UNIT(hiob)->deviceTable;
   SCSI_UINT32 i = 0;
   SCSI_UEXACT8 scsidatl;

   /* Mode switch between packetized and non-packetized transfer happenned, */
   /* we should expect a BUS FREE after the last ack for the message.       */

   /* Put the sequencer at the idle loop.  We can safely do this because */
   /* both the data channels of harpoon will be free and no SCSI         */
   /* activity will be pending for the sequencer to take care (which     */
   /* essentially means that the sequencer is really idle). */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
       (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
       (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));

   /* Disable unexpected bus free interrupt */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SIMODE1),
       OSD_INEXACT8(SCSI_AICREG(SCSI_SIMODE1)) & ~SCSI_ENBUSFREE);

   /* Ack the last message byte */
   scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));

   /* Expect a BUS FREE */
   while ((!(OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) &
          (SCSI_BUSFREE | SCSI_SCSIRSTI))) && (i++ < SCSI_REGISTER_TIMEOUT_COUNTER))
   {
      OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
   }
         
   if (i >= SCSI_REGISTER_TIMEOUT_COUNTER)
   {
      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID30);
      return((SCSI_UEXACT8)-1);
   }

   /* clear associated busy target array */
   SCSI_hTARGETCLEARBUSY(hhcb,hiob);

   /* Switching from Non-Packetized to Packetized, and vice versa, clean */
   /* up the fields in the SCB if they were modified by the sequencer    */
   /* and queue the SCB to the head of Target Execution queue.           */
   SCSI_hQHEADPNPSWITCHSCB(hhcb,hiob);

   /* Switching from Packetized to Non-Packetized.       */
   /* The current xferRate entry is already updated with */
   /* the latest negotiated rate.                        */ 

#if SCSI_PACKETIZED_IO_SUPPORT
   if (!(SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] & SCSI_PACKETIZED))
   {
      /* As this routine was called, CHIM will freeze and */
      /* redispatch all pending ios for this target.      */
      SCSI_hPACKNONPACKQUEUEHIOB(hhcb,hiob);
   }
#if SCSI_AE_NONPACKTOPACK_END
   else
   {
      /*Dispatch any IOBs that may have been queued during a switch*/
      SCSI_ASYNC_EVENT_COMMAND(hhcb,hiob,SCSI_AE_NONPACKTOPACK_END);
   }
#endif /* SCSI_AE_PACKTONONPACK_END */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

   /* Clear bus free interrupt status */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRBUSFREE);

   return((SCSI_UEXACT8)0);
}
#endif /* (!SCSI_ASYNC_NARROW_MODE && SCSI_PACKETIZED_IO_SUPPORT) */

/*********************************************************************
*
*  SCSIhPackSwitchedMsgReject
*
*     Handle drive responds message reject to get out of Packetized mode
*
*  Return Value:   0 - successful
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*           CHIM negotiates non-packetized negotiation with drive
*           which is currently negotiated in packetized mode.
*           The drive responds message reject followed by WDTR and SDTR.
*           This routine is called before the last message byte is sent
*           to the drive and requeues the command if the drive goes to
*           bus free.
*
*********************************************************************/
#if (!SCSI_ASYNC_NARROW_MODE)
SCSI_UEXACT8 SCSIhPackSwitchedMsgReject (SCSI_HHCB SCSI_HPTR hhcb,
                                         SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 phase;

   /* deassserted ATN and transfer the last byte */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);

   if (hiob->SCSI_IP_negoState == SCSI_RESPONSE_MSG07_WIDE)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL), hiob->SCSI_IP_workArea[3]);
   }
   else if (hiob->SCSI_IP_negoState == SCSI_RESPONSE_MSG07_SYNC)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL), hiob->SCSI_IP_workArea[4]);
   }

      /* Wait for a Bus Free condition or a MSG-IN phase to occur.  In the case */
   /* of a bus free condition the SCSIhWait4Req() routine, which primarily   */
   /* waits for a REQ to occur on the bus, might induce a long delay (due to */
   /* the call to OSMDelay() routine which is system dependent) before it    */
   /* could detect bus free condtion.  The delay could be long enough so that*/
   /* a reselection followed by a MSG-IN can occur on the bus by the time    */
   /* SCSIhWait4Req() detects the condition.  We should not be confused with */
   /* this MSG-IN with what we are expecting.  The MSG-IN we are expecting   */
   /* should happen without any bus free.  Therefore after SCSIhWait4Req()   */
   /* returns we need to check for the bus free condition first.  If bus free*/
   /* condition has not happened and we detect MSG-IN phase, then it must be */
   /* the one we are expecting. */
   phase = SCSIhWait4Req(hhcb);

   if ((OSD_INEXACT8(SCSI_AICREG(SCSI_HSTINTSTAT)) & SCSI_SCSIINT) &&
          (OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT1)) & SCSI_BUSFREE))
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRBUSFREE);

      /* Restart sequencer in idle loop */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                    (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 2));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                    (SCSI_UEXACT8)(SCSI_hIDLE_LOOP_ENTRY(hhcb) >> 10));

      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRHSTINT),SCSI_CLRSCSIINT);

      /* clear associated busy target array */
      SCSI_hTARGETCLEARBUSY(hhcb,hiob);

      /* Clean up the fields in the SCB if they were modified by the       */
      /* sequencer and queue the SCB to the head of Target Execution queue.*/
      SCSI_hQHEADPNPSWITCHSCB(hhcb,hiob);

      hiob->SCSI_IP_negoState = SCSI_NOT_NEGOTIATING;
   }

   return((SCSI_UEXACT8)0);
}
#endif /* (!SCSI_ASYNC_NARROW_MODE) */

/*********************************************************************
*
*  SCSIhStandardU320QHeadPNPitchSCB
*
*     Clean up the SCB and queue back to the head of Target Execution
*     queue after switching between Packetized and Non-Packetized. 
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
#if SCSI_STANDARD_U320_MODE
#if (!SCSI_ASYNC_NARROW_MODE)
void SCSIhStandardU320QHeadPNPSwitchSCB (SCSI_HHCB SCSI_HPTR hhcb,
                                         SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);

   /* Clean up the fields in the SCB if they were modified by the        */
   /* sequencer and queue the SCB to the head of Target Execution queue. */

   /* Restore lunID field */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
      OSDoffsetof(SCSI_SCB_STANDARD_U320,slun)), (SCSI_UEXACT8)targetUnit->lunID);

   /* Let the original negotiation attempt be retried for this particular  */
   /* command if the case that CHIM negotiates non-packetized negotiation  */ 
   /* with drive which is currently negotiated in packetized mode happens. */
   /* The drive has responded message reject followed by WDTR and SDTR.    */
   if ((hiob->SCSI_IP_negoState != SCSI_RESPONSE_MSG07_WIDE) &&
       (hiob->SCSI_IP_negoState != SCSI_RESPONSE_MSG07_SYNC))
   {
        /* Clear negotiation needed value in atn_length field. */
      if (hiob->SCSI_IF_tagEnable)
      {
         /* negotiation not needed for tagged queue command */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_STANDARD_U320,atn_length)), 0x03);
      }
      else
      {
         /* negotiation not needed for non-tagged queue command */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_STANDARD_U320,atn_length)), 0x01);
      }
   }
   /* Put the SCB back at the head of the execution queue */
   SCSIhStandardQHead(hhcb,hiob);

}
#endif /*(!SCSI_ASYNC_NARROW_MODE)*/
#endif /* SCSI_STANDARD_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardEnhU320QHeadPNPSwitchSCB
*
*     Clean up the SCB and queue back to the head of Target Execution
*     queue after switching between Packetized and Non-Packetized. 
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
#if SCSI_STANDARD_ENHANCED_U320_MODE
#if (!SCSI_ASYNC_NARROW_MODE)
void SCSIhStandardEnhU320QHeadPNPSwitchSCB (SCSI_HHCB SCSI_HPTR hhcb,
                                            SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);

   /* Clean up the fields in the SCB if they were modified by the        */
   /* sequencer and queue the SCB to the head of Target Execution queue. */

   /* Restore lunID field */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
      OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,slun)), (SCSI_UEXACT8)targetUnit->lunID);

   /* Let the original negotiation attempt be retried for this particular  */
   /* command if the case that CHIM negotiates non-packetized negotiation  */ 
   /* with drive which is currently negotiated in packetized mode happens. */
   /* The drive has responded message reject followed by WDTR and SDTR.    */
   if ((hiob->SCSI_IP_negoState != SCSI_RESPONSE_MSG07_WIDE) &&
       (hiob->SCSI_IP_negoState != SCSI_RESPONSE_MSG07_SYNC))
   {
        /* Clear negotiation needed value in atn_length field. */
      if (hiob->SCSI_IF_tagEnable)
      {
         /* negotiation not needed for tagged queue command */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,atn_length)), 0x03);
      }
      else
      {
         /* negotiation not needed for non-tagged queue command */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,atn_length)), 0x01);
      }
   }
   /* Put the SCB back at the head of the execution queue */
   SCSIhStandardQHead(hhcb,hiob);

}
#endif /*(!SCSI_ASYNC_NARROW_MODE)*/
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/*********************************************************************
*
*  SCSIhDchU320QHeadPNPSwitchSCB
*
*     Clean up the SCB and queue back to the head of Target Execution
*     queue after switching between Packetized and Non-Packetized. 
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320QHeadPNPSwitchSCB (SCSI_HHCB SCSI_HPTR hhcb,
                                    SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);

   /* Clean up the fields in the SCB if they were modified by the        */
   /* sequencer and queue the SCB to the head of Target Execution queue. */

   /* Restore lunID field */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
      OSDoffsetof(SCSI_SCB_DCH_U320,slun)), (SCSI_UEXACT8)targetUnit->lunID);

   /* Let the original negotiation attempt be retried for this particular  */
   /* command if the case that CHIM negotiates non-packetized negotiation  */ 
   /* with drive which is currently negotiated in packetized mode happens. */
   /* The drive has responded message reject followed by WDTR and SDTR.    */
   if ((hiob->SCSI_IP_negoState != SCSI_RESPONSE_MSG07_WIDE) &&
       (hiob->SCSI_IP_negoState != SCSI_RESPONSE_MSG07_SYNC))
   {
        /* Clear negotiation needed value in atn_length field. */
      if (hiob->SCSI_IF_tagEnable)
      {
         /* negotiation not needed for tagged queue command */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DCH_U320,atn_length)), 0x03);
      }
      else
      {
         /* negotiation not needed for non-tagged queue command */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DCH_U320,atn_length)), 0x01);
      }
   }
   /* Put the SCB back at the head of the execution queue */
   SCSIhDchU320QHead(hhcb,hiob);

}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhDownShiftU320QHeadPNPSwitchSCB
*
*     Clean up the SCB and queue back to the head of Target Execution
*     queue after switching between Packetized and Non-Packetized. 
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
#if SCSI_DOWNSHIFT_U320_MODE 
#if (!SCSI_ASYNC_NARROW_MODE)
void SCSIhDownshiftU320QHeadPNPSwitchSCB (SCSI_HHCB SCSI_HPTR hhcb,
                                         SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);

   /* Clean up the fields in the SCB if they were modified by the        */
   /* sequencer and queue the SCB to the head of Target Execution queue. */

   /* Restore lunID field */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
      OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,slun)), (SCSI_UEXACT8)targetUnit->lunID);

   /* Let the original negotiation attempt be retried for this particular  */
   /* command if the case that CHIM negotiates non-packetized negotiation  */ 
   /* with drive which is currently negotiated in packetized mode happens. */
   /* The drive has responded message reject followed by WDTR and SDTR.    */
   if ((hiob->SCSI_IP_negoState != SCSI_RESPONSE_MSG07_WIDE) &&
       (hiob->SCSI_IP_negoState != SCSI_RESPONSE_MSG07_SYNC))
   {
      /* Clear negotiation needed value in atn_length field. */
      if (hiob->SCSI_IF_tagEnable)
      {
         /* negotiation not needed for tagged queue command */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,atn_length)), 0x03);
      }
      else
      {
         /* negotiation not needed for non-tagged queue command */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,atn_length)), 0x01);
      }
   }

   /* Put the SCB back at the head of the execution queue */
   SCSIhDownshiftU320QHead(hhcb,hiob);
}
#endif /* (!SCSI_ASYNC_NARROW_MODE) */
#endif /* SCSI_DOWNSHIFT_U320_MODE */


/*********************************************************************
*
*  SCSIhDownShiftEnhU320QHeadPNPSwitchSCB
*
*     Clean up the SCB and queue back to the head of Target Execution
*     queue after switching between Packetized and Non-Packetized. 
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:
*
*********************************************************************/
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE 
#if (!SCSI_ASYNC_NARROW_MODE)
void SCSIhDownshiftEnhU320QHeadPNPSwitchSCB (SCSI_HHCB SCSI_HPTR hhcb,
                                         SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit = SCSI_TARGET_UNIT(hiob);

   /* Clean up the fields in the SCB if they were modified by the        */
   /* sequencer and queue the SCB to the head of Target Execution queue. */

   /* Restore lunID field */
   OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
      OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,slun)), (SCSI_UEXACT8)targetUnit->lunID);

   /* Let the original negotiation attempt be retried for this particular  */
   /* command if the case that CHIM negotiates non-packetized negotiation  */ 
   /* with drive which is currently negotiated in packetized mode happens. */
   /* The drive has responded message reject followed by WDTR and SDTR.    */
   if ((hiob->SCSI_IP_negoState != SCSI_RESPONSE_MSG07_WIDE) &&
       (hiob->SCSI_IP_negoState != SCSI_RESPONSE_MSG07_SYNC))
   {
      /* Clear negotiation needed value in atn_length field. */
      if (hiob->SCSI_IF_tagEnable)
      {
         /* negotiation not needed for tagged queue command */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,atn_length)), 0x03);
      }
      else
      {
         /* negotiation not needed for non-tagged queue command */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
             OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,atn_length)), 0x01);
      }
   }

   /* Put the SCB back at the head of the execution queue */
   SCSIhDownshiftU320QHead(hhcb,hiob);
}
#endif /* (!SCSI_ASYNC_NARROW_MODE) */
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */

/*********************************************************************
*
*  SCSIhGetNegoXferRate routine -
*
*     Get the transfer rate for negotiation
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 target ID
*                 transfer rate to be filled
*
*  Remarks:
*                  
*********************************************************************/
void SCSIhGetNegoXferRate (SCSI_HHCB SCSI_HPTR hhcb,
                           SCSI_UEXACT8 targetID,
                           SCSI_UEXACT8 SCSI_LPTR xferRate)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetID];
   SCSI_UEXACT8 runDtc;
   SCSI_UEXACT8 i;

   for (i = 0; i < SCSI_MAX_XFER_PARAMETERS; i++)
   {
      xferRate[i] = SCSI_NEGO_XFER(deviceTable)[i];
   }

   /* Make sure we don't go over the default xfer rate limit. */
   /* Take the lower offset. */
   if (xferRate[SCSI_XFER_OFFSET] > SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_OFFSET])
   {
      xferRate[SCSI_XFER_OFFSET] = SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_OFFSET];
   }

   /* Take the higher transfer period. */
   /* The higher transfer period means slower transfer rate. */
   if (xferRate[SCSI_XFER_PERIOD] < SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_PERIOD])
   {
      xferRate[SCSI_XFER_PERIOD] = SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_PERIOD];
   }

   /* Clear the PCOMP, RD_STRM, WR_FLOW, QAS, DT or IU bit if not enable. */
   xferRate[SCSI_XFER_PTCL_OPT] &= SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT];

   /* Clear the Wide bit if not enable. */
   xferRate[SCSI_XFER_MISC_OPT] &= SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_MISC_OPT];

   /* Set runDtc mode */
   runDtc = (xferRate[SCSI_XFER_PTCL_OPT] & SCSI_DUALEDGE) ? 1 : 0;

#if SCSI_PACKETIZED_IO_SUPPORT
   if (!(xferRate[SCSI_XFER_PTCL_OPT] & SCSI_PACKETIZED))
   {
      /* Transfer period should be 0x09 (U160 speed) or higher (slower than */
      /* U160 speed) in non-Packetized mode. */
      if (xferRate[SCSI_XFER_PERIOD] < 0x09)
      {
         xferRate[SCSI_XFER_PERIOD] = 0x09;
      }
   }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

   /* We should run in narrow and asynchronous mode if negotiation is */
   /* currently suppressed. */
   if (deviceTable->SCSI_DF_suppressNego)
   {
      xferRate[SCSI_XFER_OFFSET] = 0;              /* Asynchronous mode */
      xferRate[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;  /* Narrow bus */
      runDtc = 0;
   }
   else
   {
      /* Check the hardware capability on wide */
      if (!(OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL)) & SCSI_SELWIDE))
      {
         xferRate[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;  /* Narrow bus */
         runDtc = 0;
      }
   }

#if SCSI_LOOPBACK_OPERATION
   if (runDtc)
   {
      /* Normally negotiation enables this bit. For loopback
       * HIM has to enable.
       */
      deviceTable->SCSI_DF_negotiateDtc = 1;
   }
#endif /* SCSI_LOOPBACK_OPERATION */

#if SCSI_PPR_ENABLE
   /* Without PPR msg negotiation, the max. xfer rate CHIM can negotiate is  */
   /* Ultra-2.  This implies the transfer period should be 25ns or higher.   */
   /* The xfer period of 25ns is mapped into the xfer period factor of 0x0A. */
   if (!deviceTable->SCSI_DF_negotiateDtc)
   {
      if (xferRate[SCSI_XFER_PERIOD] < 0x0A)
      {
         xferRate[SCSI_XFER_PERIOD] = 0x0A;
      }
      runDtc = 0;
   }
#endif /* SCSI_PPR_ENABLE */

   /* Vulcan team has seen errors in signal integrity for REQ/ACKs for  */
   /* SE connections at 20MHZ but not at 10MHZ.                         */

   /* SCSI transfer rate will be at 20MB/s (Fast-20 speed or 0x0C period) */
   /* or less if                                                          */
   /* 1. A target is behind Expander/Switch Blade chip (EXP_ACTIVE bit of */
   /*    SSTAT2 reg. set) OR                                              */
   /* 2. The scsi bus is Single-End bus (ENAB20 bit of SBLKCTL reg. set)  */
   /*        AND                                                          */
   /* 3. The 'maxPeriod' value is smaller than 0x0C (Fast-20 speed).      */
   /*    The smaller of 'maxPeriod' value is the faster.                  */
#if SCSI_MAXIMUM_SE_SPEED_10

#if !SCSI_DCH_U320_MODE
   if (SCSI_hHARPOON_REV_B(hhcb))
   {
      if (((OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT2)) & SCSI_EXP_ACTIVE) ||
          (OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL)) & SCSI_ENAB20)) &&
          (xferRate[SCSI_XFER_PERIOD] < 0x0C))
      {
         xferRate[SCSI_XFER_PERIOD] = 0x0C;
         runDtc = 0;
      }
   }
   else
   {
      if (((OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT2)) & SCSI_EXP_ACTIVE) ||
          (OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL)) & SCSI_ENAB20)) &&
          (xferRate[SCSI_XFER_PERIOD] < 0x19))
      {
         xferRate[SCSI_XFER_PERIOD] = 0x19;
         runDtc = 0;
      }
   }
#else
   /* only Rev B functionality in DCH core */
   if (((OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT2)) & SCSI_EXP_ACTIVE) ||
       (OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL)) & SCSI_ENAB20)) &&
       (xferRate[SCSI_XFER_PERIOD] < 0x0C))
   {
      xferRate[SCSI_XFER_PERIOD] = 0x0C;
      runDtc = 0;
   }
#endif /* !SCSI_DCH_U320_MODE */

#else /* !SCSI_MAXIMUM_SE_SPEED_10 */

   if (((OSD_INEXACT8(SCSI_AICREG(SCSI_SSTAT2)) & SCSI_EXP_ACTIVE) ||
       (OSD_INEXACT8(SCSI_AICREG(SCSI_SBLKCTL)) & SCSI_ENAB20)) &&
       (xferRate[SCSI_XFER_PERIOD] < 0x0C))
   {
      xferRate[SCSI_XFER_PERIOD] = 0x0C;
      runDtc = 0;
   }
   
#endif /* SCSI_MAXIMUM_SE_SPEED_10 */
#if !SCSI_DOWNSHIFT_MODE
   if (runDtc)
   {
#if SCSI_STANDARD_U320_MODE
      if (SCSI_hHARPOON_REV_A(hhcb))
      {
         /* For Harpoon 2 rev A asic: the transfer period factor of 0x07 is   */
         /* meant for 320MB/s speed.  On the ther hand, the SPI-4 spec states */
         /* that the transfer period factor of 0x08 is for 320MB/s speed.     */
         if (xferRate[SCSI_XFER_PERIOD] == 0x07)
         {
            xferRate[SCSI_XFER_PERIOD] = 0x08;
         }
      }
#endif /* SCSI_STANDARD_U320_MODE */
   }
   else
#endif /* !SCSI_DOWNSHIFT_MODE */
   {
      /* Zero protocol options field */
      xferRate[SCSI_XFER_PTCL_OPT] = 0;

      /* Since we're going to negotiate for non-DT mode, the transfer speed */
      /* should be Ultra-2, Ultra, or Fast SCSI.  Hence the transfer period */
      /* should be 0x0A. */
      if (xferRate[SCSI_XFER_PERIOD] < 0x0A)
      {
         xferRate[SCSI_XFER_PERIOD] = 0x0A;
      }
   }
}

/*********************************************************************
*
*  SCSIhCheckSyncNego routine -
*
*     Readjust the synchronous negotiation parameters based
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*
*  Remarks:                
*                  
*********************************************************************/
void SCSIhCheckSyncNego (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_UEXACT8 i;
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
   /* SCSI_UEXACT8 j; */
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */

   /* Re-Initialize sync/wide negotiation parameters depending on */
   /* whether SuppressNego flag is set                            */
   for (i = 0; i < hhcb->maxDevices; i++)   
   {

#if SCSI_ASYNC_NARROW_MODE
      SCSIhAsyncNarrowXferRateAssign(hhcb,i);
#else

#if SCSI_NEGOTIATION_PER_IOB
      /* Unless forced through IOB we should never negotiate; so, assume */
      /* async/narrow */
      SCSIhAsyncNarrowXferRateAssign(hhcb,i);

#else /* !SCSI_NEGOTIATION_PER_IOB */

      deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];
#if SCSI_TARGET_OPERATION
          /* if target mode enabled ONLY then don't set 
           * SCSI_NEEDNEGO unless target initiated negotiation
           * is enabled - our current rates should be 
           * async and narrow.
           */
      if (SCSI_hWIDEORSYNCNEGONEEDED(deviceTable) &&
          (hhcb->SCSI_HF_initiatorMode ||
           (hhcb->SCSI_HF_targetMode && hhcb->SCSI_HF_targetInitNegotiation)))
#else
      if (SCSI_hWIDEORSYNCNEGONEEDED(deviceTable))
#endif /* SCSI_TARGET_OPERATION */
      {
         /*  Set negotiation needed */
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
         /* Need to set negotiation for all possible IDs */
         /* This code needs to be reworked, for now just 
          * use initiator mode call.
          */
         /* for (j = 0; j < hhcb->maxDevices; j++) */   
         {
            /* SCSI_hMULTIPLEIDXFEROPTASSIGN(hhcb,i,j,SCSI_NEEDNEGO); */
            /*  Set negotiation needed */
            SCSIhUpdateNeedNego(hhcb,i);
         } 
#else
         /*  Set negotiation needed */
         SCSIhUpdateNeedNego(hhcb,i);
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
      }
      else
      {
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
         /* Need to set negotiation for all possible IDs */
         /* This code needs to be reworked, for now just 
          * use initiator mode call.
          */
         /* Need to set negotiation for all possible IDs */
         /* for (j = 0; j < hhcb->maxDevices; j++) */  
         {
            /* SCSI_hMULTIPLEIDXFEROPTASSIGN(hhcb,i,j,0x00); */
            SCSIhAsyncNarrowXferRateAssign(hhcb,i);
         } 
#else
         SCSIhAsyncNarrowXferRateAssign(hhcb,i);
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
      }         

#endif /* SCSI_NEGOTIATION_PER_IOB */
#endif /* SCSI_ASYNC_NARROW_MODE */
   }
}

/*********************************************************************
*
*  SCSIhUpdateNeedNego routine -
*
*     Update SCSI_DF_needNego flag and update the atn_length field
*     in SCB for specified target ID that is at the head of Target
*     Execution Queue or New Queue.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 target ID / option index
*
*  Remarks:
*                  
*********************************************************************/
#if (!SCSI_ASYNC_NARROW_MODE)
void SCSIhUpdateNeedNego (SCSI_HHCB SCSI_HPTR hhcb,
                          SCSI_UEXACT8 targetID)
{
#if SCSI_BIOS_SUPPORT
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
#endif /* SCSI_BIOS_SUPPORT */

#if SCSI_BIOS_SUPPORT

   if (!hhcb->SCSI_HF_biosRunTimeEnabled)
   {
      SCSI_DEVICE_TABLE(hhcb)[targetID].SCSI_DF_needNego = 1;
   }   
   else
   {
      if (targetID < 8) 
      {      
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_NEGO_ON_MODE_SWITCH),
            (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_NEGO_ON_MODE_SWITCH)) | 1 << targetID));
      }
      else
      {
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_NEGO_ON_MODE_SWITCH+1),
            (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_NEGO_ON_MODE_SWITCH+1)) | 1 << targetID - 8));
      }
   }   

#else /* !SCSI_BIOS_SUPPORT */

   SCSI_DEVICE_TABLE(hhcb)[targetID].SCSI_DF_needNego = 1;

   /* Update atn_length field of a SCB that is at the head of specified */
   /* target's Execution queue if one exist. */
   if (!SCSI_hUPDATEEXEQATNLENGTH(hhcb, targetID))
   {
      /* Update atn_length field of a host SCB for the specified target ID */
      /* that is the first to match the targetID in the New queue.*/
      SCSI_hUPDATENEWQATNLENGTH(hhcb, targetID);
   }

#endif /* SCSI_BIOS_SUPPORT */
}
#endif /* (!SCSI_ASYNC_NARROW_MODE) */

/*********************************************************************
*
*  SCSIhAsyncNarrowXferRateAssign routine -
*
*     Assign Async and Narrow transfer rate into the device table's
*     current xferRate and the hardware NEGODATA table.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 target ID
*
*  Remarks:
*
*********************************************************************/
void SCSIhAsyncNarrowXferRateAssign (SCSI_HHCB SCSI_HPTR hhcb,
                                     SCSI_UEXACT8 targetID)
{
   SCSI_DEVICE SCSI_DPTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetID];

#if SCSI_BIOS_SUPPORT
   if (!hhcb->SCSI_HF_biosRunTimeEnabled)
#endif /* SCSI_BIOS_SUPPORT */
   {
      /* Update current xfer rate entry to be async and narrow mode */
      SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
      SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;
      SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] = 0;
      SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_MISC_OPT] = 0;
   }

   /* Update hardware NEGODATA table */
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
   if (SCSI_hHARPOON_REV_1B(hhcb) &&
       hhcb->SCSI_HF_targetMode &&
       (!hhcb->SCSI_HF_initiatorMode))
   {
      /* Eventually a for loop of SCSI enabled IDs would be required. */
      SCSI_hLOADNEGODATA(hhcb,
                         (SCSI_UEXACT8)(targetID | (hhcb->hostScsiID << 4)),
                         SCSI_CURRENT_XFER(deviceTable));
   }
   else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
   {
      SCSI_hLOADNEGODATA(hhcb,targetID,SCSI_CURRENT_XFER(deviceTable));
   }
}

/*********************************************************************
*
*  SCSIhXferRateAssign routine -
*
*     Assign the transfer rate to device table's current xferRate
*     and hardware negodata registers.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 target ID / option index
*                 value to be assigned
*
*  Remarks:
*
*********************************************************************/
void SCSIhXferRateAssign (SCSI_HHCB SCSI_HPTR hhcb,
                          SCSI_UEXACT8 targetID,
                          SCSI_UEXACT8 SCSI_LPTR xferRate)
{
   SCSI_DEVICE SCSI_DPTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetID];
   SCSI_UEXACT8 i;

   /* Assign the transfer rate fields */
   for (i = 0; i < SCSI_MAX_XFER_PARAMETERS; i++)
   {
#if SCSI_BIOS_SUPPORT
      if (!hhcb->SCSI_HF_biosRunTimeEnabled)
#endif /* SCSI_BIOS_SUPPORT */
      {
         /* Update current xfer rate entry */
         SCSI_CURRENT_XFER(deviceTable)[i] = xferRate[i];
      }
   }

   /* Update hardware NEGODATA table */
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
   if (SCSI_hHARPOON_REV_1B(hhcb) &&
       hhcb->SCSI_HF_targetMode &&
       (!hhcb->SCSI_HF_initiatorMode))
   {
      /* Eventually a for loop of SCSI enabled IDs would be required. */
      SCSI_hLOADNEGODATA(hhcb,
                         (SCSI_UEXACT8)(targetID | (hhcb->hostScsiID << 4)),
                         xferRate);
   }
   else
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */
   {
      SCSI_hLOADNEGODATA(hhcb,targetID,xferRate);
   }
}

/*********************************************************************
*
*  SCSIhMultipleHostIdXferOptAssign routine -
*
*     Assign the transfer option fields for negotiation for modes with
*     xfer option residing in host memory
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 target ID / option index
*                 hostID / selectedID
*                 value to be assigned
*
*  Remarks:
*
*********************************************************************/
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
#if (SCSI_SWAPPING_MODE)
void SCSIhMultipleHostIdXferOptAssign (SCSI_HHCB SCSI_HPTR hhcb,
                                       SCSI_UEXACT8 targetID,
                                       SCSI_UEXACT8 hostID,
                                       SCSI_UEXACT16 value)
{
   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_UEXACT16 scbNumber;

   /* Assign the transfer option fields that common to both */
   /* "with the check condition" or "without" */
   SCSI_hMULTIID_COMMON_XFER_ASSIGN(hhcb, targetID, hostID, value);
   
   /* For all SCBs in ACTPTR array, update xfer_option if the same target ID */
   for (scbNumber = 0; scbNumber < hhcb->numberScbs; scbNumber++)
   {
      if ((hiob = SCSI_ACTPTR[scbNumber]) != SCSI_NULL_HIOB)
      {
         if (hiob->cmd == SCSI_CMD_ESTABLISH_CONNECTION)
         {
            /* no xfer options associated with Establish Connection
             * SCBs.
             */
            continue;
         }   
         if (hiob->SCSI_IP_targetMode)
         {
            /* obtain IDs from Nexus */
            if ((targetID == SCSI_NEXUS_UNIT(hiob)->scsiID) &&
                (hostID == SCSI_NEXUS_UNIT(hiob)->selectedID))
            {
               SCSI_hUPDATE_XFER_OPTION_HOST_MEM(hhcb,hiob,value);
            }
         } 
         else
         if (targetID == SCSI_TARGET_UNIT(hiob)->scsiID)
         {
            SCSI_hUPDATE_XFER_OPTION_HOST_MEM(hhcb,hiob,value);
         }
      }
   }

}
#endif /* (SCSI_SWAPPING_MODE) */
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */

/*********************************************************************
*
*  SCSIhStandardU320UpdateExeQAtnLength
*
*     Update atn_length field in a SCB for specified Target ID
*     that is at the head of Target Execution Queue.
*
*  Return Value:  1 - successful
*                 0 - failure
*
*  Parameters:    hhcb
*                 targetID
*  Remarks:       
*                 
*********************************************************************/
#if SCSI_STANDARD_U320_MODE
SCSI_UEXACT8 SCSIhStandardU320UpdateExeQAtnLength (SCSI_HHCB SCSI_HPTR hhcb,
                                                   SCSI_UEXACT8 targetID)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetID];
   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT16 scbNumber;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 found = 0;

   /* Paused the chip */
   if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   /* Save SCBPTR */
   scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
             ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

   /* Get scbNumber in Head of Execution queue */
   scbNumber = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD)) |
                ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1))) << 8);

   while (!SCSI_IS_NULL_SCB(scbNumber))
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbNumber>>8));

      /* Check if the SCB has the same target ID. Must Check low nybble */
      /* only since Sequencer uses high nybble for other purposes */
      if (targetID == (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                       OSDoffsetof(SCSI_SCB_STANDARD_U320, starget))) & 0x0F)) 
      {
         /* get associated hiob */
         hiob = SCSI_ACTPTR[scbNumber];
         if (hiob->SCSI_IF_tagEnable)         
         {
            if (deviceTable->SCSI_DF_needNego)
            {
               /* negotiation needed for tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_STANDARD_U320,atn_length)),0x04);            

               deviceTable->SCSI_DF_needNego = 0;
            }          
            else
            {         
               /* negotiation not needed for tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_STANDARD_U320,atn_length)),0x03);
            }
         }
         else
         {
            if (deviceTable->SCSI_DF_needNego)
            {
                /* negotiation needed for non-tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_STANDARD_U320,atn_length)),0x02);
               deviceTable->SCSI_DF_needNego = 0;
            }
            else
            {
                /* negotiation not needed for non-tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_STANDARD_U320,atn_length)),0x01);
            }                     
         }
         found = 1;
         break;   /* out of while loop */
      }
      /* Advance to next SCB in Execution queue */
      scbNumber = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_STANDARD_U320, SCSI_SU320_q_next))) |
          ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_STANDARD_U320, SCSI_SU320_q_next)+1))) << 8);
   }

   /* Restore SCBPTR */   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbptr>>8));

   /* Unpaused the chip */  
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
   return(found);
}
#endif /* SCSI_STANDARD_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardEnhU320UpdateExeQAtnLength
*
*     Update atn_length field in a SCB for specified Target ID
*     that is at the head of Target Execution Queue.
*
*  Return Value:  1 - successful
*                 0 - failure
*
*  Parameters:    hhcb
*                 targetID
*  Remarks:       
*                 
*********************************************************************/
#if SCSI_STANDARD_ENHANCED_U320_MODE
SCSI_UEXACT8 SCSIhStandardEnhU320UpdateExeQAtnLength (SCSI_HHCB SCSI_HPTR hhcb,
                                                      SCSI_UEXACT8 targetID)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetID];
   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT16 scbNumber;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 found = 0;

   /* Paused the chip */
   if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   /* Save SCBPTR */
   scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
             ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

   /* Get scbNumber in Head of Execution queue */
   scbNumber = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD)) |
                ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_Q_EXE_HEAD+1))) << 8);

   while (!SCSI_IS_NULL_SCB(scbNumber))
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbNumber>>8));

      /* Check if the SCB has the same target ID. Must Check low nybble */
      /* only since Sequencer uses high nybble for other purposes */
      if (targetID == (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320, starget))) & 0x0F)) 
      {
         /* get associated hiob */
         hiob = SCSI_ACTPTR[scbNumber];
         if (hiob->SCSI_IF_tagEnable)         
         {
            if (deviceTable->SCSI_DF_needNego)
            {
               /* negotiation needed for tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,atn_length)),0x04);            

               deviceTable->SCSI_DF_needNego = 0;
            }          
            else
            {         
               /* negotiation not needed for tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,atn_length)),0x03);
            }
         }
         else
         {
            if (deviceTable->SCSI_DF_needNego)
            {
                /* negotiation needed for non-tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,atn_length)),0x02);
               deviceTable->SCSI_DF_needNego = 0;
            }
            else
            {
                /* negotiation not needed for non-tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,atn_length)),0x01);
            }                     
         }
         found = 1;
         break;   /* out of while loop */
      }
      /* Advance to next SCB in Execution queue */
      scbNumber = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320, SCSI_SEU320_q_next))) |
          ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320, SCSI_SEU320_q_next)+1))) << 8);
   }

   /* Restore SCBPTR */   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbptr>>8));

   /* Unpaused the chip */  
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
   return(found);
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/*********************************************************************
*
*  SCSIhDchU320UpdateExeQAtnLength
*
*     Update atn_length field in a SCB for specified Target ID
*     that is at the head of Target Execution Queue.
*
*  Return Value:  1 - successful
*                 0 - failure
*
*  Parameters:    hhcb
*                 targetID
*  Remarks:       
*                 
*********************************************************************/
#if SCSI_DCH_U320_MODE
SCSI_UEXACT8 SCSIhDchU320UpdateExeQAtnLength (SCSI_HHCB SCSI_HPTR hhcb,
                                              SCSI_UEXACT8 targetID)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetID];
   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_UEXACT16 scbptr;
   SCSI_UEXACT16 scbNumber;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 found = 0;

   /* Paused the chip */
   if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   /* Save SCBPTR */
   scbptr = (SCSI_UEXACT16)OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR)) |
             ((SCSI_UEXACT16)(OSD_INEXACT8(SCSI_AICREG(SCSI_SCBPTR+1))) << 8);

   /* Get scbNumber in Head of Execution queue */
   scbNumber = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD)) |
                ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_Q_EXE_HEAD+1))) << 8);

   while (!SCSI_IS_NULL_SCB(scbNumber))
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbNumber);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbNumber>>8));

      /* Check if the SCB has the same target ID. Must Check low nybble */
      /* only since Sequencer uses high nybble for other purposes */
      if (targetID == (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                       OSDoffsetof(SCSI_SCB_DCH_U320, starget))) & 0x0F)) 
      {
         /* get associated hiob */
         hiob = SCSI_ACTPTR[scbNumber];
         if (hiob->SCSI_IF_tagEnable)         
         {
            if (deviceTable->SCSI_DF_needNego)
            {
               /* negotiation needed for tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_DCH_U320,atn_length)),0x04);            

               deviceTable->SCSI_DF_needNego = 0;
            }          
            else
            {         
               /* negotiation not needed for tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_DCH_U320,atn_length)),0x03);
            }
         }
         else
         {
            if (deviceTable->SCSI_DF_needNego)
            {
                /* negotiation needed for non-tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_DCH_U320,atn_length)),0x02);
               deviceTable->SCSI_DF_needNego = 0;
            }
            else
            {
                /* negotiation not needed for non-tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_DCH_U320,atn_length)),0x01);
            }                     
         }
         found = 1;
         break;   /* out of while loop */
      }
      /* Advance to next SCB in Execution queue */
      scbNumber = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next))) |
          ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_DCH_U320, SCSI_DCHU320_q_next)+1))) << 8);
   }

   /* Restore SCBPTR */   
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR), (SCSI_UEXACT8)scbptr);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCBPTR+1), (SCSI_UEXACT8)(scbptr>>8));

   /* Unpaused the chip */  
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
   return(found);
}
#endif /* SCSI_DCH_U320_MODE */
/*********************************************************************
*
*  SCSIhDownshiftU320UpdateExeQAtnLength
*
*     Update atn_length field in a SCB for specified Target ID
*     that is at the head of Target Execution Queue.
*
*  Return Value:  1 - successful
*                 0 - failure
*
*  Parameters:    hhcb
*                 targetID
*  Remarks:       
*                 
*********************************************************************/
#if (SCSI_DOWNSHIFT_MODE && (!SCSI_BIOS_SUPPORT))
SCSI_UEXACT8 SCSIhDownshiftU320UpdateExeQAtnLength (SCSI_HHCB SCSI_HPTR hhcb,
                                                    SCSI_UEXACT8 targetID)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetID];
   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_UEXACT16 scbNumber;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 found = 0;

   /* Paused the chip */
   if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);
   }

   /* Get scbNumber in Head of Execution queue */
   scbNumber = (SCSI_UEXACT16)OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_Q_EXE_HEAD)) |
                ((SCSI_UEXACT16)(OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_Q_EXE_HEAD+1))) << 8);

   hiob = SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET];
   
   if (hiob != SCSI_NULL_HIOB)
   {
      if (hiob->SCSI_IF_tagEnable)         
      {
         if (deviceTable->SCSI_DF_needNego)
         {
#if SCSI_DOWNSHIFT_U320_MODE
           if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
          
              /* negotiation needed for tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,atn_length)),0x04);            
#endif /* SCSI_DOWNSHIFT_U320_MODE */            
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE	
           if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)  
              /* negotiation needed for tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,atn_length)),0x04);            
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */           
            deviceTable->SCSI_DF_needNego = 0;
         }          
         else
         {   
#if SCSI_DOWNSHIFT_U320_MODE
             if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
             
               /* negotiation not needed for tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,atn_length)),0x03);
            
#endif /* SCSI_DOWNSHIFT_U320_MODE */
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
             if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)        
               /* negotiation not needed for tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,atn_length)),0x03);
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */           
         }
      }
      else
      {
         if (deviceTable->SCSI_DF_needNego)
         {
#if SCSI_DOWNSHIFT_U320_MODE    
            if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
             
                /* negotiation needed for non-tagged queue command */
                OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,atn_length)),0x02);
#endif /* SCSI_DOWNSHIFT_U320_MODE */           
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
            if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)    
                /* negotiation needed for non-tagged queue command */
                OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                      OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,atn_length)),0x02);
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */            
            deviceTable->SCSI_DF_needNego = 0;
         }
         else
         {
#if SCSI_DOWNSHIFT_U320_MODE
            if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
         
               /* negotiation not needed for non-tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,atn_length)),0x01);
#endif /* SCSI_DOWNSHIFT_U320_MODE */            
    
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE    
             if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)
             /* negotiation not needed for non-tagged queue command */
               OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
                     OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,atn_length)),0x01);
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */
            
         }                     
      }
      found = 1;
   }
   /* Unpaused the chip */  
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
   return(found);
}
#endif /* (SCSI_DOWNSHIFT_MODE && (!SCSI_BIOS_SUPPORT)) */


/*********************************************************************
*
*  SCSIhStandardUpdateNewQAtnLength
*
*     Update atn_length field in a SCB that matched a specified
*     Target ID in New Queue.
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 targetID
*  Remarks:       
*                 
*
*********************************************************************/
#if SCSI_STANDARD_MODE
void SCSIhStandardUpdateNewQAtnLength (SCSI_HHCB SCSI_HPTR hhcb,
                                       SCSI_UEXACT8 targetID)
{
   SCSI_SCB_DESCRIPTOR SCSI_HPTR scbDescriptor;
   SCSI_HIOB SCSI_IPTR hiob = SCSI_NULL_HIOB;
   SCSI_BUS_ADDRESS scbBusAddress;
   SCSI_UEXACT16 scbNumber;

   /* Obtain scbnumber sitting at the head of the new queue and its        */
   /* corresponding hiob.  To get the scbNumber we need to get the         */
   /* q_new_pointer first and then search the active  pointer array to get */
   /* the hiob containing the same address. */
#if SCSI_ASPI_SUPPORT
   SCSI_hGETADDRESSSCRATCH(hhcb,SCSI_hQ_NEW_POINTER(hhcb));
   scbBusAddress = hhcb->busAddress;
#else
   scbBusAddress = SCSI_hGETADDRESSSCRATCH(hhcb,SCSI_hQ_NEW_POINTER(hhcb));
#endif /* SCSI_ASPI_SUPPORT */
   for (scbNumber = 0; scbNumber < hhcb->numberScbs; scbNumber++)
   {
      /* Obtain HIOB in Active pointer array */ 
      hiob = SCSI_ACTPTR[scbNumber];

      if (hiob != SCSI_NULL_HIOB)
      {
         if (!SCSIhCompareBusAddress(hiob->scbDescriptor->scbBuffer.busAddress,
             scbBusAddress))
         {
            break;
         }
         else if (scbNumber == (hhcb->numberScbs-1))
         {
            hiob = SCSI_NULL_HIOB;
         }
      }
   }

   /* Have the search completed? */
   while (hiob != SCSI_NULL_HIOB)
   {
#if SCSI_TARGET_OPERATION
      if (hiob->cmd == SCSI_CMD_ESTABLISH_CONNECTION)
      {
         /* No xfer options associated with Establish Connection
          * SCBs. Note for swapping advanced mode there should 
          * not be Establish Connection SCBs in the qNew.
          * However, this is not true for standard advanced mode. 
          */
         ;
      }
      if (hiob->SCSI_IP_targetMode)
      {
         /* obtain ID from Nexus */
         if (targetID == SCSI_NEXUS_UNIT(hiob)->scsiID)
         {
            SCSI_hSETUPATNLENGTH(hhcb,hiob);
         }
      } 
      else
#endif /* SCSI_TARGET_OPERATION */
      if (targetID == SCSI_TARGET_UNIT(hiob)->scsiID)
      {
         SCSI_hSETUPATNLENGTH(hhcb,hiob);
      }

      /* .....Obtain the ScbNumber */   
      scbDescriptor = hiob->scbDescriptor->queueNext;
      scbNumber = scbDescriptor->scbNumber;
 
      /* We reach the end of the new queue if the current scb is NULL. */
      /* So, just exit the search. */
      if (SCSI_IS_NULL_SCB(scbNumber))
      {
         break;
      }
      else
      {
         /* ... obtain HIOB in Active pointer array */
         hiob = SCSI_ACTPTR[scbNumber];
      }
   }
}
#endif /* SCSI_STANDARD_MODE */

/*********************************************************************
*
*  SCSIhDownshiftUpdateNewQAtnLength
*
*     Update atn_length field in a SCB that matched a specified
*     Target ID in New Queue.
*
*  Return Value:  none
*
*  Parameters:    hhcb
*                 targetID
*  Remarks:       
*                 
*
*********************************************************************/
#if (SCSI_DOWNSHIFT_MODE && (!SCSI_BIOS_SUPPORT))
void SCSIhDownshiftUpdateNewQAtnLength (SCSI_HHCB SCSI_HPTR hhcb,
                                        SCSI_UEXACT8 targetID)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_BUS_ADDRESS scbBusAddress;

   /* Obtain scbnumber sitting at the head of the new queue and its        */
   /* corresponding hiob.  To get the scbNumber we need to get the         */
   /* q_new_pointer first and then search the active  pointer array to get */
   /* the hiob containing the same address. */
#if SCSI_ASPI_SUPPORT
   SCSI_hGETADDRESSSCRATCH(hhcb,SCSI_hQ_NEW_POINTER(hhcb));
   scbBusAddress = hhcb->busAddress;
#else
   scbBusAddress = SCSI_hGETADDRESSSCRATCH(hhcb,SCSI_hQ_NEW_POINTER(hhcb));
#endif /* SCSI_ASPI_SUPPORT */
   /* Obtain HIOB in Active pointer array */ 
   hiob = SCSI_ACTPTR[SCSI_ACTPTR_DOWNSHIFT_OFFSET];

   if (hiob != SCSI_NULL_HIOB)
   {
      if (targetID == SCSI_TARGET_UNIT(hiob)->scsiID)
      {
         SCSI_hSETUPATNLENGTH(hhcb,hiob);
      }
   }
}
#endif /* (SCSI_DOWNSHIFT_MODE && (!SCSI_BIOS_SUPPORT)) */

/*********************************************************************
*
*  SCSIhMultipleHostIdGetOption routine -
*
*  Get the current transfer option fields for negotiation for standard
*  enhanced mode.
*
*  Return Value:  option value
*                  
*  Parameters:    hhcb
*                 option index
*                 our host ID for this operation 
*
*  Remarks:  To be implemented.              
*                  
*********************************************************************/
#if (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT)
#if SCSI_STANDARD_ENHANCED_U320_MODE
SCSI_UEXACT16 SCSIhMultipleHostIdGetOption (SCSI_HHCB SCSI_HPTR hhcb,
                                            SCSI_UEXACT8 index,
                                            SCSI_UEXACT8 hostID)
{
   return(0);
   /* return(SCSI_DEVICE_TABLE(hhcb)[index].xferOptionHost[hostID]); */
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */
#endif /* (SCSI_TARGET_OPERATION && SCSI_MULTIPLEID_SUPPORT) */

/*********************************************************************
*
*  SCSIhLastNegoEntryAssign
*
*  Assign SCSI_LAST_NEGO_ENTRY to be the current negoXferIndex and
*  assign the SCSI_CURRENT_ENTRY into this entry
*
*  Return Value:  None
*                  
*  Parameters:    deviceTable
*
*  Remarks:                
*                  
*********************************************************************/
void SCSIhLastNegoEntryAssign(SCSI_DEVICE SCSI_DPTR deviceTable)
{
   SCSI_UEXACT8 i;

   /* Make SCSI_LAST_NEGO_XFER entry to be the current negoXferIndex.    */
   /* This is needed for the subsequent negotiations and one of them is  */
   /* SCSI request sense command (non-packetized) after check condition. */
   deviceTable->negoXferIndex = SCSI_LAST_NEGO_ENTRY;

   /* keep the last negotiated parameters */
   for (i = 0; i < SCSI_MAX_XFER_PARAMETERS; i++)
   {
      SCSI_LAST_NEGO_XFER(deviceTable)[i] = SCSI_CURRENT_XFER(deviceTable)[i];
   }
}

/*********************************************************************
*
*  SCSIhModifyDataPtr
*
*     Handle Modify Data Pointer message
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:                
*                  
*********************************************************************/
void SCSIhModifyDataPtr (SCSI_HHCB SCSI_HPTR hhcb,
                         SCSI_HIOB SCSI_IPTR hiob)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 phase;
   SCSI_UEXACT8 scsidatl;

   /* Reject the modify data pointer message */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MIPHASE | SCSI_ATNO);
   do {
      scsidatl = OSD_INEXACT8(SCSI_AICREG(SCSI_SCSIDATL));
      phase = SCSIhWait4Req(hhcb);
   } while (phase == SCSI_MIPHASE);
   if (phase != SCSI_MOPHASE)
   {
      SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID40);
   }
   else
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSISIGO), SCSI_MOPHASE);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_CLRSINT1), SCSI_CLRATNO);
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SCSIDATL), SCSI_MSG07);
      if (SCSIhWait4Req(hhcb) == (SCSI_UEXACT8)-1)
      {
         SCSIhBadSeq(hhcb,(SCSI_UEXACT8)SCSI_BADSEQ_CALLID41);
         return;
      }
      /* set reentrant address for sequencer */
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT0),
                    (SCSI_UEXACT8)(SCSI_hSIOSTR3_ENTRY(hhcb) >> 2));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_PRGMCNT1),
                    (SCSI_UEXACT8)(SCSI_hSIOSTR3_ENTRY(hhcb) >> 10));
   }
}

/*********************************************************************
*
*  SCSIhEvenIOLength
*
*     Scan the S/G list to determine if I/O was even length.
*
*  Return Value:  1 - even length
*                 0 - odd length
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:                
*                 Called from the SCSIhxxxIgnoreWideResidueCalc  
*                 routines. NOTE: DCH_U320_MODE is the exception
*                  
*********************************************************************/
#if !SCSI_DCH_U320_MODE
SCSI_UEXACT8 SCSIhEvenIOLength (SCSI_HHCB SCSI_HPTR hhcb,
                                SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_BUFFER_DESCRIPTOR SCSI_LPTR sgList;
   void SCSI_LPTR virtualAddress;
   SCSI_UEXACT32 sgLength;
   SCSI_UEXACT16 i;
   SCSI_UEXACT8 oddLength;
   SCSI_UEXACT8 currentSGListSize = sizeof(SCSI_BUS_ADDRESS);
   SCSI_UINT32 count;

#if (OSM_BUS_ADDRESS_SIZE == 64)
   if (hhcb->SCSI_HF_SgBusAddress32)
   {
      currentSGListSize = 4;
   }
#endif /* (OSM_BUS_ADDRESS_SIZE == 64) */
   
   /* Walk down the s/g list to determine if odd length - if odd
    * length then ignore wide residue should not cause an 
    * underrun. 
    */ 
      
   /* Get the S/G List (its a buffer descriptor) */
   sgList = OSD_GET_SG_LIST(hiob);
   virtualAddress = sgList->virtualAddress;
        
   /* Walk down S/G list elements. When the delimiter 
    * is encountered, or timeout counter expires then exit.
    */
   count = (SCSI_UINT32)0x800000;
   i = 0;
   oddLength = 0;

   while(1)
   {
      SCSI_GET_LITTLE_ENDIAN32(hhcb,&sgLength,virtualAddress,
           (i*2*currentSGListSize)+currentSGListSize);

      if (sgLength & (SCSI_UEXACT32)0x00000001)
      {
         oddLength++;
      }
      
      /* to handle one segment and zero in length passed in */
      if ((sgLength & (SCSI_UEXACT32)0x80000000) ||
          (--count == 0))
      {
         break;
      }
      i++;
   }

   if (oddLength & (SCSI_UEXACT8)0x01)
   {
      return(0);
   }
   else
   {
      return(1);
   }   
}
#endif /* !SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  SCSIhDchEvenIOLength
*
*     Scan the S/G list to determine if I/O was even length.
*
*  Return Value:  1 - even length
*                 0 - odd length
*                  
*  Parameters:    hhcb
*                 hiob
*
*  Remarks:                
*                 Called from the SCSIhDchIgnoreWideResidueCalc  
*                 routines.
*                  
*********************************************************************/
#if SCSI_DCH_U320_MODE
SCSI_UEXACT8 SCSIhDchEvenIOLength (SCSI_HHCB SCSI_HPTR hhcb,
                                SCSI_HIOB SCSI_IPTR hiob)
{
   SCSI_BUFFER_DESCRIPTOR SCSI_LPTR sgList;
   void SCSI_LPTR virtualAddress;
   SCSI_BUS_ADDRESS busAddress;
   SCSI_UEXACT32 sgLength;
   SCSI_UEXACT32 sgFlags;
   SCSI_UEXACT16 i;
   SCSI_UEXACT8 oddLength;

#if (OSM_BUS_ADDRESS_SIZE == 64)
   /* should not occur */
   SCSI_UEXACT8 currentSGListSize = sizeof(SCSI_BUS_ADDRESS);
#else
   SCSI_UEXACT8 currentSGListSize = sizeof(SCSI_BUS_ADDRESS)*2;
#endif /* (OSM_BUS_ADDRESS_SIZE == 64) */
   SCSI_UINT32 count;

   /* Walk down the s/g list to determine if odd length - if odd
    * length then ignore wide residue should not cause an 
    * underrun. 
    */ 
      
   /* Get the S/G List (its a buffer descriptor) */
   sgList = OSD_GET_SG_LIST(hiob);
   virtualAddress = sgList->virtualAddress;
        
   /* Walk down S/G list elements. When the delimiter 
    * is encountered, or timeout counter expires then exit.
    */
   count = (SCSI_UINT32)0x800000;
   i = 0;
   oddLength = 0;

   while(1)
   {
      SCSI_GET_LITTLE_ENDIAN32(hhcb,&sgLength,virtualAddress,
           (i*2*currentSGListSize)+currentSGListSize);

      SCSI_GET_LITTLE_ENDIAN32(hhcb,&sgFlags,virtualAddress,
           (i*2*currentSGListSize)+(currentSGListSize+4));

      if (sgLength & (SCSI_UEXACT32)0x00000001)
      {
         oddLength++;
      }

      /* check for LL (last list terminates SGL) */
      if ((sgFlags & (SCSI_UEXACT32)0x40000000) || (--count == 0))
      {
         break;
      }

      /* are we at the last element in this list ? */
      else if ((sgFlags & (SCSI_UEXACT32)0x80000000) || (--count == 0))
      {
         /* 
            $$$ DCH note: For now all SGL addresses are within the IOP
            memory space (32 bit). 
         */

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
   }

   if (oddLength & (SCSI_UEXACT8)0x01)
   {
      return(0);
   }
   else
   {
      return(1);
   }   
}
#endif /* SCSI_DCH_U320_MODE */


/*********************************************************************
*
*  SCSIhStandardU320IgnoreWideResidueCalc
*
*     Calculate ignore wide residual length.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*                 bytesToIgnore  number of bytes to ignore 
*                                (should be 1)
*
*  Remarks:                
*                 Entered as a result of receipt of an Ignore Wide 
*                 Residue message.
*                  
*********************************************************************/
#if SCSI_STANDARD_U320_MODE
void SCSIhStandardU320IgnoreWideResidueCalc (SCSI_HHCB SCSI_HPTR hhcb,
                                             SCSI_HIOB SCSI_IPTR hiob,
                                             SCSI_UEXACT8 bytesToIgnore)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_QUADLET regValue;
   SCSI_UEXACT8 modeSave;

   /* Save the incoming mode, and put in the mode that we got the interrupt, */
   /* which should be either 0, or 1 */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);

   /* **** This routine does not work for all cases **** */   
   /* Currently this algorithm will only work if ignore wide
    * residue message occurs at end of data transfer - normal
    * case. Further work is required to handle ignore wide
    * residue on intermediate data transfers (highly unlikely).
    *
    * This routine handles the following two cases on the 
    * last data transfer:
    * a) all data transfered but ignore wide residue on last
    *    byte. This is indicated by sg_cache_work = no_data. 
    * b) residual in SCB is non-zero   
    *
    * For case a) above the residue is not set up by the
    * sequencer. This would be a normal completion. Therefore,
    * we need to manually set the hiob haStat to indicate a 
    * data underrun and set the hiob residual value to the 
    * bytesToIgnore value.
    * For case b) simply read the residual value from the SCB
    * and add the bytesToIgnore.
    * Looking at the sg_cache_work (maybe also sg_cache_SCB would work)
    * to see if there is no_data left to be transferred - case a).
    */
   if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_sg_cache_work))) &
       SCSI_SU320_NODATA)
   {
      /* If NO_DATA set then we've exhausted s/g list and are at
       * the end of the transfer. Special handling is required 
       * for this case as the check length interrupt handler 
       * does not process end of transfer with message in phase
       * switch.  Only generate underrun if
       * overall I/O length was even, otherwise we'll 
       * generate a false underun. E.g. If I/O request was 
       * for 27 bytes, and 28 bytes were returned with a 
       * an ignore wide residue message then we don't want
       * to generate an underrun.       */
      if ((SCSI_hEVENIOLENGTH(hhcb,hiob)) && (hiob->haStat != SCSI_HOST_DU_DO))
      {
         /*
          * Set underrun status and initialize the hiob
          * residual field.
          */
         hiob->residualLength = (SCSI_UEXACT32)bytesToIgnore;
         hiob->haStat = SCSI_HOST_DU_DO;     /* indicate underrun status */
         hiob->stat = SCSI_SCB_ERR;
         hiob->trgStatus = SCSI_UNIT_GOOD;   /* SCSIxTranslateError, to update */
      }
   }
   else
   {
      /* The sequencer will generate a check_length interrupt for
       * this case so we only need to modify the residual count in the
       * SCB.
       * SCB residue0-3 already contains the residue so far. Add
       * bytesToIgnore value. Sequencer only uses 3 bytes of residual.
       */
      regValue.u8.order0 = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_RESCNT_BASE+0));
      regValue.u8.order1 = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_RESCNT_BASE+1));
      regValue.u8.order2 = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_RESCNT_BASE+2));
      regValue.u8.order3 = 0;
      *((SCSI_UEXACT32 *) &regValue) += (SCSI_UEXACT32)bytesToIgnore;

      /* Update the SCB residual field. */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_RESCNT_BASE+0),
                         regValue.u8.order0);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_RESCNT_BASE+1),
                         regValue.u8.order1);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_RESCNT_BASE+2),
                         regValue.u8.order2);
   }

   /* Restore back the mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
}
#endif /* SCSI_STANDARD_U320_MODE */

/*********************************************************************
*
*  SCSIhStandardEnhU320IgnoreWideResidueCalc
*
*     Calculate ignore wide residual length.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*                 bytesToIgnore  number of bytes to ignore 
*                                (should be 1)
*
*  Remarks:                
*                 Entered as a result of receipt of an Ignore Wide 
*                 Residue message.
*                  
*********************************************************************/
#if SCSI_STANDARD_ENHANCED_U320_MODE
void SCSIhStandardEnhU320IgnoreWideResidueCalc (SCSI_HHCB SCSI_HPTR hhcb,
                                                SCSI_HIOB SCSI_IPTR hiob,
                                                SCSI_UEXACT8 bytesToIgnore)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_QUADLET regValue;
   SCSI_UEXACT8 modeSave;

   /* Save the incoming mode, and put in the mode that we got the interrupt, */
   /* which should be either 0, or 1 */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);

   /* **** This routine does not work for all cases **** */   
   /* Currently this algorithm will only work if ignore wide
    * residue message occurs at end of data transfer - normal
    * case. Further work is required to handle ignore wide
    * residue on intermediate data transfers (highly unlikely).
    *
    * This routine handles the following two cases on the 
    * last data transfer:
    * a) all data transfered but ignore wide residue on last
    *    byte. This is indicated by sg_cache_work = no_data. 
    * b) residual in SCB is non-zero   
    *
    * For case a) above the residue is not set up by the
    * sequencer. This would be a normal completion. Therefore,
    * we need to manually set the hiob haStat to indicate a 
    * data underrun and set the hiob residual value to the 
    * bytesToIgnore value.
    * For case b) simply read the residual value from the SCB
    * and add the bytesToIgnore.
    * Looking at the sg_cache_work (maybe also sg_cache_SCB would work)
    * to see if there is no_data left to be transferred - case a).
    */
   if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_sg_cache_work))) &
       SCSI_SEU320_NODATA)
   {
      /* If NO_DATA set then we've exhausted s/g list and are at
       * the end of the transfer. Special handling is required 
       * for this case as the check length interrupt handler 
       * does not process end of transfer with message in phase
       * switch.  Only generate underrun if
       * overall I/O length was even, otherwise we'll 
       * generate a false underun. E.g. If I/O request was 
       * for 27 bytes, and 28 bytes were returned with a 
       * an ignore wide residue message then we don't want
       * to generate an underrun.       */
      if ((SCSI_hEVENIOLENGTH(hhcb,hiob)) && (hiob->haStat != SCSI_HOST_DU_DO))
      {
         /*
          * Set underrun status and initialize the hiob
          * residual field.
          */
         hiob->residualLength = (SCSI_UEXACT32)bytesToIgnore;
         hiob->haStat = SCSI_HOST_DU_DO;     /* indicate underrun status */
         hiob->stat = SCSI_SCB_ERR;
         hiob->trgStatus = SCSI_UNIT_GOOD;   /* SCSIxTranslateError, to update */
      }
   }
   else
   {
      /* The sequencer will generate a check_length interrupt for
       * this case so we only need to modify the residual count in the
       * SCB.
       * SCB residue0-3 already contains the residue so far. Add
       * bytesToIgnore value. Sequencer only uses 3 bytes of residual.
       */
      regValue.u8.order0 = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_RESCNT_BASE+0));
      regValue.u8.order1 = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_RESCNT_BASE+1));
      regValue.u8.order2 = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_RESCNT_BASE+2));
      regValue.u8.order3 = 0;
      *((SCSI_UEXACT32 *) &regValue) += (SCSI_UEXACT32)bytesToIgnore;

      /* Update the SCB residual field. */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_RESCNT_BASE+0),
                         regValue.u8.order0);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_RESCNT_BASE+1),
                         regValue.u8.order1);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_SU320_RESCNT_BASE+2),
                         regValue.u8.order2);
   }

   /* Restore back the mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/*********************************************************************
*
*  SCSIhDchU320IgnoreWideResidueCalc
*
*     Calculate ignore wide residual length.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*                 bytesToIgnore  number of bytes to ignore 
*                                (should be 1)
*
*  Remarks:                
*                 Entered as a result of receipt of an Ignore Wide 
*                 Residue message.
*                  
*********************************************************************/
#if SCSI_DCH_U320_MODE
void SCSIhDchU320IgnoreWideResidueCalc (SCSI_HHCB SCSI_HPTR hhcb,
                                        SCSI_HIOB SCSI_IPTR hiob,
                                        SCSI_UEXACT8 bytesToIgnore)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_QUADLET regValue;
   SCSI_UEXACT8 modeSave;

   /* Save the incoming mode, and put in the mode that we got the interrupt, */
   /* which should be either 0, or 1 */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

   SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);

   /* **** This routine does not work for all cases **** */   
   /* Currently this algorithm will only work if ignore wide
    * residue message occurs at end of data transfer - normal
    * case. Further work is required to handle ignore wide
    * residue on intermediate data transfers (highly unlikely).
    *
    * This routine handles the following two cases on the 
    * last data transfer:
    * a) all data transfered but ignore wide residue on last
    *    byte. This is indicated by sg_cache_work = no_data. 
    * b) residual in SCB is non-zero   
    *
    * For case a) above the residue is not set up by the
    * sequencer. This would be a normal completion. Therefore,
    * we need to manually set the hiob haStat to indicate a 
    * data underrun and set the hiob residual value to the 
    * bytesToIgnore value.
    * For case b) simply read the residual value from the SCB
    * and add the bytesToIgnore.
    * Looking at the sg_cache_work (maybe also sg_cache_SCB would work)
    * to see if there is no_data left to be transferred - case a).
    */
   if (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
       OSDoffsetof(SCSI_SCB_DCH_U320,sg_cache_SCB))) &
       SCSI_DCHU320_NODATA)
   {
      /* If NO_DATA set then we've exhausted s/g list and are at
       * the end of the transfer. Special handling is required 
       * for this case as the check length interrupt handler 
       * does not process end of transfer with message in phase
       * switch.  Only generate underrun if
       * overall I/O length was even, otherwise we'll 
       * generate a false underun. E.g. If I/O request was 
       * for 27 bytes, and 28 bytes were returned with a 
       * an ignore wide residue message then we don't want
       * to generate an underrun.       */
      if ((SCSI_hEVENIOLENGTH(hhcb,hiob)) && (hiob->haStat != SCSI_HOST_DU_DO))
      {
         /*
          * Set underrun status and initialize the hiob
          * residual field.
          */
         hiob->residualLength = (SCSI_UEXACT32)bytesToIgnore;
         hiob->haStat = SCSI_HOST_DU_DO;     /* indicate underrun status */
         hiob->stat = SCSI_SCB_ERR;
         hiob->trgStatus = SCSI_UNIT_GOOD;   /* SCSIxTranslateError, to update */
      }
   }
   else
   {
      /* The sequencer will generate a check_length interrupt for
       * this case so we only need to modify the residual count in the
       * SCB.
       * SCB residue0-3 already contains the residue so far. Add
       * bytesToIgnore value. Sequencer only uses 3 bytes of residual.
       */
      regValue.u8.order0 = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_RESCNT_BASE+0));
      regValue.u8.order1 = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_RESCNT_BASE+1));
      regValue.u8.order2 = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_RESCNT_BASE+2));
      regValue.u8.order3 = 0;
      *((SCSI_UEXACT32 *) &regValue) += (SCSI_UEXACT32)bytesToIgnore;

      /* Update the SCB residual field. */
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_RESCNT_BASE+0),
                         regValue.u8.order0);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_RESCNT_BASE+1),
                         regValue.u8.order1);
      OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DCHU320_RESCNT_BASE+2),
                         regValue.u8.order2);
   }

   /* Restore back the mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
}
#endif /* SCSI_DCH_U320_MODE */
/*********************************************************************
*
*  SCSIhDownshiftU320IgnoreWideResidueCalc
*
*     Calculate ignore wide residual length.
*
*  Return Value:  None
*                  
*  Parameters:    hhcb
*                 hiob
*                 bytesToIgnore  number of bytes to ignore 
*                                (should be 1)
*
*  Remarks:                
*                 Entered as a result of receipt of an Ignore Wide 
*                 Residue message.
*                  
*********************************************************************/
#if SCSI_DOWNSHIFT_MODE
void SCSIhDownshiftU320IgnoreWideResidueCalc (SCSI_HHCB SCSI_HPTR hhcb,
                                              SCSI_HIOB SCSI_IPTR hiob,
                                              SCSI_UEXACT8 bytesToIgnore)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_QUADLET regValue;
   SCSI_UEXACT8 modeSave;
#if SCSI_BIOS_SUPPORT
   SCSI_UEXACT8 bios_originalmode;
#endif /* SCSI_BIOS_SUPPORT */
   SCSI_UEXACT8 sg_cache_work;
   SCSI_UEXACT8 no_data;
   /* Save the incoming mode, and put in the mode that we got the interrupt, */
   /* which should be either 0, or 1 */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);

#if SCSI_BIOS_SUPPORT
   SCSIHReadScratchRam(hhcb,
                       (SCSI_UEXACT8 SCSI_SPTR) &bios_originalmode,
                       (SCSI_DOWNSHIFT_SCRATCH_START_ADRS + SCSI_BIOS_ORIGINALMODE),
                       (SCSI_UEXACT16) 1);
   SCSI_hSETMODEPTR(hhcb,scsiRegister,bios_originalmode);
#else /* !SCSI_BIOS_SUPPORT */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,hhcb->SCSI_HP_originalMode);
#endif /* SCSI_BIOS_SUPPORT */

   /* **** This routine does not work for all cases **** */   
   /* Currently this algorithm will only work if ignore wide
    * residue message occurs at end of data transfer - normal
    * case. Further work is required to handle ignore wide
    * residue on intermediate data transfers (highly unlikely).
    *
    * This routine handles the following two cases on the 
    * last data transfer:
    * a) all data transfered but ignore wide residue on last
    *    byte. This is indicated by sg_cache_work = no_data. 
    * b) residual in SCB is non-zero   
    *
    * For case a) above the residue is not set up by the
    * sequencer. This would be a normal completion. Therefore,
    * we need to manually set the hiob haStat to indicate a 
    * data underrun and set the hiob residual value to the 
    * bytesToIgnore value.
    * For case b) simply read the residual value from the SCB
    * and add the bytesToIgnore.
    * Looking at the sg_cache_work (maybe also sg_cache_SCB would work)
    * to see if there is no_data left to be transferred - case a).
    */
 
 #if SCSI_DOWNSHIFT_U320_MODE
 
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_U320)
   {
         sg_cache_work = (OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DOWNSHIFT_U320,SCSI_DU320_sg_cache_work))));
         no_data = SCSI_DU320_NODATA;		 
         
   }
 
 #endif /* SCSI_DOWNSHIFT_U320_MODE */
 #if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
 
   if (hhcb->firmwareMode == SCSI_FMODE_DOWNSHIFT_ENHANCED_U320)
   {
         sg_cache_work = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_SCB_BASE+
         OSDoffsetof(SCSI_SCB_DOWNSHIFT_ENH_U320,SCSI_DEU320_sg_cache_work)));
         no_data = SCSI_DEU320_NODATA;
          
   }
 
 #endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */
 
   
    if (sg_cache_work & no_data)
   
    {
      /* If NO_DATA set then we've exhausted s/g list and are at
       * the end of the transfer. Special handling is required 
       * for this case as the check length interrupt handler 
       * does not process end of transfer with message in phase
       * switch.  Only generate underrun if
       * overall I/O length was even, otherwise we'll 
       * generate a false underun. E.g. If I/O request was 
       * for 27 bytes, and 28 bytes were returned with a 
       * an ignore wide residue message then we don't want
       * to generate an underrun.       */
      if ((SCSI_hEVENIOLENGTH(hhcb,hiob)) && (hiob->haStat != SCSI_HOST_DU_DO))
      {
         /* 
          * Set underrun status and initialize the hiob
          * residual field.
          */
         hiob->residualLength = (SCSI_UEXACT32)bytesToIgnore;
         hiob->haStat = SCSI_HOST_DU_DO;     /* indicate underrun status */
         hiob->stat = SCSI_SCB_ERR;
         hiob->trgStatus = SCSI_UNIT_GOOD;   /* SCSIxTranslateError, to update */
      }
   }
   else
   {
      /* The sequencer will generate a check_length interrupt for
       * this case so we only need to modify the residual count in the
       * SCB.
       * SCB residue0-3 already contains the residue so far. Add
       * bytesToIgnore value. Sequencer only uses 3 bytes of residual.
       */
     
         regValue.u8.order0 = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_RESCNT_BASE+0));
         regValue.u8.order1 = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_RESCNT_BASE+1));
         regValue.u8.order2 = OSD_INEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_RESCNT_BASE+2));
         regValue.u8.order3 = 0;
         *((SCSI_UEXACT32 *) &regValue) += (SCSI_UEXACT32)bytesToIgnore;

         /* Update the SCB residual field. */
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_RESCNT_BASE+0),
                            regValue.u8.order0);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_RESCNT_BASE+1),
                            regValue.u8.order1);
         OSD_OUTEXACT8_HIGH(SCSI_AICREG(SCSI_DU320_RESCNT_BASE+2),
                            regValue.u8.order2);
      
      

   }

   /* Restore back the mode */
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);
}
#endif /* SCSI_DOWNSHIFT_MODE */


/*********************************************************************
*
*  SCSIhSetXferRate routine
*
*     Set the transfer rates to the default value.
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 scsiID
*
*  Remarks:       Only verified for Ultra320 adapters/ASICs                
*                  
*********************************************************************/
#if SCSI_LOOPBACK_OPERATION
void SCSIhSetXferRate (SCSI_HHCB SCSI_HPTR hhcb,
                       SCSI_UEXACT8 scsiID)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_DEVICE SCSI_DPTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[scsiID];
   SCSI_UEXACT8 xferRate[SCSI_MAX_XFER_PARAMETERS];
   SCSI_HIOB SCSI_IPTR hiob;
   SCSI_HIOB fakeHiob;
   SCSI_UNIT_CONTROL fakeUnitControl;
   
   /* Certain negotiation rate routines require an HIOB as 
    * parameter to get to the device table. 
    * Therefore, create a fake HIOB along with a fake targetUnit
    * to satisfy routines. Note that this routine may be call from 
    * target mode also.
    */
   hiob = &fakeHiob;

   OSDmemset(hiob,0,sizeof(SCSI_HIOB));   

   /* Assign to HIOB */
   SCSI_TARGET_UNIT(hiob) = &fakeUnitControl;

   /* build fake unit control */
   fakeUnitControl.hhcb = hhcb;
   fakeUnitControl.scsiID = scsiID;
   fakeUnitControl.lunID = 0;     
   fakeUnitControl.deviceTable = deviceTable;
   fakeUnitControl.targetControl = 0;

   /* Get nego xfer rate */
   SCSI_hGETNEGOXFERRATE(hhcb,scsiID,xferRate);

   SCSI_hSETNEGOTIATEDSTATE(hhcb,scsiID,1);

   /* Warning: we have to use the actual routine here as the
    * macro SCSI_hLOADNEGODATA is NULL when SCSI_LOOPBACK_OPERATION 
    * is enabled.
    */
   SCSIhLoadNegoData(hhcb,scsiID,xferRate);

}
#endif /* SCSI_LOOPBACK_OPERATION */



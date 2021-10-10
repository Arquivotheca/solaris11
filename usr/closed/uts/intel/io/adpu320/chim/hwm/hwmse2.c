/*$Header: /vobs/u320chim/src/chim/hwm/hwmse2.c   /main/21   Thu Aug 21 17:31:07 2003   luu $*/

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
*  Module Name:   HWMSE2.C
*
*  Description:
*                 Codes access SEEPROM attached to host device
*
*  Owners:        ECX IC Firmware Team
*
*    
*  Notes:         It's up to the OSD to determine if the code should
*                 stay resident after initialization.
*
*  Entry Point(s):
*                 SCSIHSizeSEEPROM
*                 SCSIHReadSEEPROM
*                 SCSIHWriteSEEPROM
*
***************************************************************************/

#include "scsi.h"
#include "hwm.h"

/*********************************************************************
*
*  SCSIHSizeSEEPROM
*
*     Get size of SEEPROM supported
*
*  Return Value:  size of SEEPROM in words (16 bits)
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine return the size of SEEPROM supported
*                 by host device. There is no guaranteee that the SEEPROM
*                 is attached to the specified host device.
*                  
*********************************************************************/
#if (SCSI_SEEPROM_ACCESS && !SCSI_BIOS_SUPPORT && !SCSI_BEF_SUPPORT)
SCSI_UINT32 SCSIHSizeSEEPROM (SCSI_HHCB SCSI_HPTR hhcb)
{
         
   return(256);  /* only support AT93C66 at this time */
}
#endif /* (SCSI_SEEPROM_ACCESS && !SCSI_BIOS_SUPPORT && !SCSI_BEF_SUPPORT) */

/*********************************************************************
*
*  SCSIHReadSEEPROM
*
*     Read from SEEPROM attached to host device
*
*  Return Value:  SCSI_SUCCESS - read successfully
*                 SCSI_FAILURE - read was unsuccessful.
*                  
*  Parameters:    hhcb
*                 dataBuffer to be filled 
*                 offset (in words) relative to the beginning of SEEPROM
*                 length (in words) of information to be read from SEEPROM
*
*  Remarks:       This routine assumes no understanding of how 
*                 information get layed out on SEEPROM. It's caller's
*                 responsibility to know what to read.
*                 Each word is 16 bits (2 bytes).
*                  
*********************************************************************/
SCSI_UINT8 SCSIHReadSEEPROM (SCSI_HHCB SCSI_HPTR hhcb,
                             SCSI_UEXACT8 SCSI_SPTR dataBuffer,
                             SCSI_SINT16 wordAddress,
                             SCSI_SINT16 wordLength)
{
#if SCSI_SEEPROM_ACCESS
   if (hhcb->hardwareMode == SCSI_HMODE_AICU320)
   {
      /* Use hardware state machine to access SEEPROM */
      if (SCSIhU320ReadSEEPROM(hhcb, dataBuffer, wordAddress, wordLength) == 0)
      {
         return(SCSI_SUCCESS);
      }
   }
   return(SCSI_FAILURE);
#else
   return(SCSI_FAILURE);
#endif /* SCSI_SEEPROM_ACCESS */
}

/*********************************************************************
*
*  SCSIHWriteSEEPROM
*
*     Write to SEEPROM attached to host device
*
*  Return Value:  SCSI_SUCCESS - write successfully
*                 SCSI_FAILURE - write was unsuccessful or SEEPROM 
*                                was unavailable
*                  
*  Parameters:    hhcb
*                 offset (in words) relative to the beginning of SEEPROM
*                 length (in words) of information to written to SEEPROM
*                 dataBuffer
*
*  Remarks:       This routine assumes no understanding of how 
*                 information get layed out on SEEPROM. It's caller's
*                 responsibility to know what will be written.
*                  
*********************************************************************/
SCSI_UINT8 SCSIHWriteSEEPROM (SCSI_HHCB SCSI_HPTR hhcb,
                              SCSI_SINT16 wordAddress,
                              SCSI_SINT16 wordLength,
                              SCSI_UEXACT8 SCSI_SPTR dataBuffer)
{
#if (SCSI_SEEPROM_ACCESS  && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT)
   if (hhcb->hardwareMode == SCSI_HMODE_AICU320)
   {
      /* Use hardware state machine to access SEEPROM */
      if (SCSIhU320WriteSEEPROM(hhcb, dataBuffer, wordAddress, wordLength) == 0)
      {
         return(SCSI_SUCCESS);
      }
   }
   return(SCSI_FAILURE);
#else
   return(SCSI_FAILURE);
#endif /* (SCSI_SEEPROM_ACCESS && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */
}

/*********************************************************************
*
*  SCSIhU320ReadSEEPROM
*
*     Read from SEEPROM attached to U320 based host device
*
*  Return Value:  status
*                 0 - read successfully
*                  
*  Parameters:    hhcb
*                 dataBuffer to be filled 
*                 offset (in words) relative to the beginning of SEEPROM
*                 length (in words) of information to be read from SEEPROM
*
*  Remarks:       This routine assumes no understanding of how 
*                 information get layed out on SEEPROM. It's caller's
*                 responsibility to know what to read.
*                 Each word is 16 bits (2 bytes).
*                  
*********************************************************************/
#if (SCSI_SEEPROM_ACCESS && !SCSI_EFI_BIOS_SUPPORT)
int SCSIhU320ReadSEEPROM (SCSI_HHCB SCSI_HPTR hhcb,
                          SCSI_UEXACT8 SCSI_SPTR dataBuffer,
                          SCSI_SINT16 wordAddress,
                          SCSI_SINT16 wordLength)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_INT i, cnt;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 modeSave;
   SCSI_INT retStatus = 0; 
   
   /* check if pause chip necessary */   
   if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);      
   }   
  
   /* Mode 3 required */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);  
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
   for (i = 0; i < wordLength; i++)
   {
      cnt = 2000;
      
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2ADR), (SCSI_UEXACT8)((wordAddress+i)&0x00FF));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2CST_CTL), SCSI_SE2_READ | SCSI_START);

      /* Wait for busy and request in progress to de-assert */
      while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SE2CST_STAT)) &
                 (SCSI_SEEARBACK | SCSI_BUSY)) && cnt)
      {
         OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
         cnt--;
      }

      if (!cnt)
      {
         retStatus = 1;  /* we got time-out (50 us) */
      }
      else
      {
         ((SCSI_DOUBLET SCSI_LPTR)(dataBuffer + i * 2))->u8.order0 = 
            OSD_INEXACT8(SCSI_AICREG(SCSI_SE2DAT0));
         ((SCSI_DOUBLET SCSI_LPTR)(dataBuffer + i * 2))->u8.order1 = 
            OSD_INEXACT8(SCSI_AICREG(SCSI_SE2DAT1));
      }                         
        
   }
   
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);     
   
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
   return(retStatus);
}
#endif /* (SCSI_SEEPROM_ACCESS  && !SCSI_EFI_BIOS_SUPPORT) */

/*********************************************************************
*
*  SCSIhU320WriteSEEPROM
*
*     Write to SEEPROM attached to U320 based host device
*
*  Return Value:  status
*                 0 - write successfully
*                  
*  Parameters:    hhcb
*                 dataBuffer 
*                 offset (in words) relative to the beginning of SEEPROM
*                 length (in words) of information to be writen to SEEPROM
*
*  Remarks:       This routine assumes no understanding of how 
*                 information get layed out on SEEPROM. It's caller's
*                 responsibility to know what to read.
*                 Each word is 16 bits (2 bytes).
*                  
*********************************************************************/

#if (SCSI_SEEPROM_ACCESS && !SCSI_EFI_BIOS_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) 
int SCSIhU320WriteSEEPROM (SCSI_HHCB SCSI_HPTR hhcb,
                           SCSI_UEXACT8 SCSI_SPTR dataBuffer,
                           SCSI_SINT16 wordAddress,
                           SCSI_SINT16 wordLength)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_INT i,cnt;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 modeSave;  
   SCSI_INT retStatus = 0; 

   /* check if pause chip necessary */
   if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);      
   }

   /* Mode 3 required */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);  
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
   /* setup address port for write enable op-code */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2ADR), SCSI_SE2_EWEN_ADDR);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2CST_CTL), SCSI_SE2_EWEN | SCSI_START);
   cnt = 5000;

   /* Wait for busy and request in progress to de-assert*/
   while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SE2CST_STAT)) & 
                                (SCSI_SEEARBACK|SCSI_BUSY)) && cnt)
   {
      OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      cnt--;
   }

   if (!cnt)
   {
      retStatus = 1;  /* we got time-out (50 us) */
   }
   else
   {
      /* load initial address */
      for (i = 0; i < wordLength; i++)
      {
         cnt = 5000;

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2DAT0),
             ((SCSI_DOUBLET SCSI_LPTR)(dataBuffer + i * 2))->u8.order0 );
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2DAT1),
             ((SCSI_DOUBLET SCSI_LPTR)(dataBuffer + i * 2))->u8.order1 );

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2ADR), (SCSI_UEXACT8)((wordAddress+i)&0x00FF));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2CST_CTL), SCSI_SE2_WRITE | SCSI_START);

         /* Wait for busy and request in progress to de-assert*/
         while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SE2CST_STAT)) &
                       (SCSI_SEEARBACK | SCSI_BUSY)) && cnt)
         {
            OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
            cnt--;
         }

         if (!cnt)
         {
            retStatus = 1;  /* we got time-out (50 us) */
         } 

      }
   }

   /* setup address port for write disable op-code */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2ADR), SCSI_SE2_EWDS_ADDR);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2CST_CTL), SCSI_SE2_EWDS | SCSI_START);
   cnt = 5000;
   
   /* Wait for busy and request in progress to de-assert*/
   while ((OSD_INEXACT8(SCSI_AICREG(SCSI_SE2CST_STAT)) &
                         (SCSI_SEEARBACK | SCSI_BUSY)) && cnt)
   {
      OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
      cnt--;
   }

   if (!cnt)
   {
      retStatus = 1;  /* we got time-out (50 us) */
   }

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);     
   
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }

   return(retStatus);   
}
#endif /* (SCSI_SEEPROM_ACCESS && !SCSI_EFI_BIOS_SUPPORT && !SCSI_ASPI_SUPPORT_GROUP1 && !SCSI_BEF_SUPPORT) */

/*********************************************************************
*
*  SCSIhWaitForSEEPROMReady
*
*     Wait for the SEEPROM to become ready.
*
*  Return Value:  status
*                 SCSI_SUCCESS - SEERPOM ready
*                 SCSI_FAILURE - timed out
*                  
*  Parameters:    hhcb
*                 cnt of SCSI_MINIMUM_DELAY_COUNT interations in polling loop
*
*  Remarks:       This is a Special routine for SCSI_EFI_BIOS_SUPPORT
*                  
*********************************************************************/
#if (SCSI_SEEPROM_ACCESS && SCSI_EFI_BIOS_SUPPORT)
SCSI_UINT8 SCSIhWaitForSEEPROMReady (SCSI_HHCB SCSI_HPTR hhcb,
                                     SCSI_INT cnt)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_UEXACT8 result;

   /* Wait for busy and request in progress to de-assert */
   return OSD_POLLEXACT8(SCSI_AICREG(SCSI_SE2CST_STAT),
      (SCSI_SEEARBACK | SCSI_BUSY),                    /* mask          */
      0,                                               /* desired value */
      ((cnt * SCSI_MINIMUM_DELAY_COUNT * 1000) / 100), /* 100ns units   */
      &result);
}
#endif /* (SCSI_SEEPROM_ACCESS  && SCSI_EFI_BIOS_SUPPORT) */

/*********************************************************************
*
*  SCSIhU320ReadSEEPROM
*
*     Read from SEEPROM attached to U320 based host device
*
*  Return Value:  status
*                 0 - read successfully
*                  
*  Parameters:    hhcb
*                 dataBuffer to be filled 
*                 offset (in words) relative to the beginning of SEEPROM
*                 length (in words) of information to be read from SEEPROM
*
*  Remarks:       This routine assumes no understanding of how 
*                 information get layed out on SEEPROM. It's caller's
*                 responsibility to know what to read.
*                 Each word is 16 bits (2 bytes).
*                  
*********************************************************************/
#if (SCSI_SEEPROM_ACCESS && SCSI_EFI_BIOS_SUPPORT)
int SCSIhU320ReadSEEPROM (SCSI_HHCB SCSI_HPTR hhcb,
                          SCSI_UEXACT8 SCSI_SPTR dataBuffer,
                          SCSI_SINT16 wordAddress,
                          SCSI_SINT16 wordLength)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_INT i;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 modeSave;
   SCSI_INT retStatus = 0; 

   /* check if pause chip necessary */   
   if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);      
   }   
  
   /* Mode 3 required */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);  
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
   for (i = 0; i < wordLength; i++)
   {
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2ADR), (SCSI_UEXACT8)((wordAddress+i)&0x00FF));
      OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2CST_CTL), SCSI_SE2_READ | SCSI_START);

      /* Wait for busy and request in progress to de-assert */
      if (SCSIhWaitForSEEPROMReady(hhcb, 2000)) 
         
      {
         retStatus = 1;  /* we got time-out (50 us) */
      }
      else
      {
         ((SCSI_DOUBLET SCSI_LPTR)(dataBuffer + i * 2))->u8.order0 = 
            OSD_INEXACT8(SCSI_AICREG(SCSI_SE2DAT0));
         ((SCSI_DOUBLET SCSI_LPTR)(dataBuffer + i * 2))->u8.order1 = 
            OSD_INEXACT8(SCSI_AICREG(SCSI_SE2DAT1));
      }                         
   }
   
   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);     
   
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }
   return(retStatus);
}
#endif /* (SCSI_SEEPROM_ACCESS && SCSI_EFI_BIOS_SUPPORT) */

/*********************************************************************
*
*  SCSIhU320WriteSEEPROM
*
*     Write to SEEPROM attached to U320 based host device
*
*  Return Value:  status
*                 0 - write successfully
*                  
*  Parameters:    hhcb
*                 dataBuffer 
*                 offset (in words) relative to the beginning of SEEPROM
*                 length (in words) of information to be writen to SEEPROM
*
*  Remarks:       This routine assumes no understanding of how 
*                 information get layed out on SEEPROM. It's caller's
*                 responsibility to know what to read.
*                 Each word is 16 bits (2 bytes).
*                  
*********************************************************************/
#if (SCSI_SEEPROM_ACCESS && SCSI_EFI_BIOS_SUPPORT) 
int SCSIhU320WriteSEEPROM (SCSI_HHCB SCSI_HPTR hhcb,
                           SCSI_UEXACT8 SCSI_SPTR dataBuffer,
                           SCSI_SINT16 wordAddress,
                           SCSI_SINT16 wordLength)
{
   register SCSI_REGISTER scsiRegister = hhcb->scsiRegister;
   SCSI_INT i;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 modeSave;  
   SCSI_INT retStatus = 0; 

   /* check if pause chip necessary */
   if (!((hcntrl = OSD_INEXACT8(SCSI_AICREG(SCSI_HCNTRL))) & SCSI_PAUSEACK))
   {
      SCSIhPauseAndWait(hhcb);      
   }

   /* Mode 3 required */
   modeSave = SCSI_hGETMODEPTR(hhcb,scsiRegister);  
   SCSI_hSETMODEPTR(hhcb,scsiRegister,SCSI_MODE3);
   
   /* setup address port for write enable op-code */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2ADR), SCSI_SE2_EWEN_ADDR);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2CST_CTL), SCSI_SE2_EWEN | SCSI_START);

   /* Wait for busy and request in progress to de-assert*/
   if (SCSIhWaitForSEEPROMReady(hhcb, 5000))
   {
      retStatus = 1;  /* we got time-out (50 us) */
   }
   else
   {
      /* load initial address */
      for (i = 0; i < wordLength; i++)
      {
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2DAT0),
             ((SCSI_DOUBLET SCSI_LPTR)(dataBuffer + i * 2))->u8.order0 );
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2DAT1),
             ((SCSI_DOUBLET SCSI_LPTR)(dataBuffer + i * 2))->u8.order1 );

         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2ADR), (SCSI_UEXACT8)((wordAddress+i)&0x00FF));
         OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2CST_CTL), SCSI_SE2_WRITE | SCSI_START);

         /* Wait for busy and request in progress to de-assert*/
         if (SCSIhWaitForSEEPROMReady(hhcb, 5000))
         {
            retStatus = 1;  /* we got time-out (50 us) */
         } 
      }
   }

   /* setup address port for write disable op-code */
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2ADR), SCSI_SE2_EWDS_ADDR);
   OSD_OUTEXACT8(SCSI_AICREG(SCSI_SE2CST_CTL), SCSI_SE2_EWDS | SCSI_START);
   
   /* Wait for busy and request in progress to de-assert*/
   if (SCSIhWaitForSEEPROMReady(hhcb, 5000))
   {
      retStatus = 1;  /* we got time-out (50 us) */
   }

   SCSI_hSETMODEPTR(hhcb,scsiRegister,modeSave);     
   
   if (!(hcntrl & SCSI_PAUSEACK))
   {
      SCSI_hWRITE_HCNTRL(hhcb, hcntrl);
   }

   return(retStatus);   
}
#endif /* (SCSI_SEEPROM_ACCESS && SCSI_EFI_BIOS_SUPPORT) */


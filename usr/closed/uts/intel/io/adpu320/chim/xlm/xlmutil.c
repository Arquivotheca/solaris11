/*$Header: /vobs/u320chim/src/chim/xlm/xlmutil.c   /main/132   Tue May  6 22:15:58 2003   spal3094 $*/

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
*  Module Name:   XLMUTIL.C
*
*  Description:
*                 Codes to implement run time Translation Management Layer
*                 protocol.
*
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         NONE
*
*  Entry Point(s):
*
***************************************************************************/
#include "scsi.h"
#include "xlm.h"

/*********************************************************************
*
*  OSDBuildSCB
*
*     Build scb buffer
*
*  Return Value:  void
*                  
*  Parameters:    hiob
*
*  Remarks:       This routine is defined for internal HIM reference.
*
*********************************************************************/
void OSDBuildSCB (SCSI_HIOB SCSI_IPTR hiob)
{
   HIM_IOB HIM_PTR iob = SCSI_GETIOB(hiob);
   SCSI_TARGET_TSCB HIM_PTR targetTSH;
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH;

   targetTSH = SCSI_TRGTSH(iob->taskSetHandle);
   adapterTSH = SCSI_ADPTSH(targetTSH->adapterTSH);

   /* invoke the proper routine to build SCB */
   adapterTSH->OSDBuildSCB(hiob,iob,targetTSH,adapterTSH);
}

/*********************************************************************
*
*  OSDStandardU320BuildSCB
*
*     Build scb buffer for standard Ultra 320 mode
*
*  Return Value:  void
*                  
*  Parameters:    hiob
*
*  Remarks:       This routine is defined for internal HIM reference
*
*********************************************************************/
#if SCSI_STANDARD_U320_MODE
void OSDStandardU320BuildSCB (SCSI_HIOB HIM_PTR hiob,
                              HIM_IOB HIM_PTR iob,
                              SCSI_TARGET_TSCB HIM_PTR targetTSH,
                              SCSI_ADAPTER_TSCB HIM_PTR adapterTSH)
{
   SCSI_IOB_RESERVE HIM_PTR iobReserve =
                     (SCSI_IOB_RESERVE HIM_PTR)iob->iobReserve.virtualAddress;
   SCSI_SCB_STANDARD_U320 HIM_PTR scbBuffer =
      (SCSI_SCB_STANDARD_U320 HIM_PTR) hiob->scbDescriptor->scbBuffer.virtualAddress;
   SCSI_BUS_ADDRESS busAddress;
   SCSI_UEXACT32 sgData;
   SCSI_UEXACT8 sg_cache_SCB;
   SCSI_UEXACT8 scontrol;
   SCSI_UEXACT8 slun;
#if ((OSM_BUS_ADDRESS_SIZE == 64) || SCSI_SELTO_PER_IOB)
   SCSI_HHCB HIM_PTR hhcb = &SCSI_ADPTSH(adapterTSH)->hhcb;
#endif /* ((OSM_BUS_ADDRESS_SIZE == 64) || SCSI_SELTO_PER_IOB) */
   SCSI_UEXACT8 currentSGListSize = sizeof(SCSI_BUS_ADDRESS);
   SCSI_UEXACT8 scontrol1 = 0;                               

#if (OSM_BUS_ADDRESS_SIZE == 64)
   if (hhcb->SCSI_HF_SgBusAddress32)
   {
      currentSGListSize = 4;
   }
#endif /* (OSM_BUS_ADDRESS_SIZE == 64) */

   scontrol = 0;     /* Assume disconnect is disabled */
   slun = (SCSI_UEXACT8) targetTSH->lunID;
   if ((SCSI_DEVICE_TABLE(&adapterTSH->hhcb)[(SCSI_UEXACT8)SCSI_TRGTSH(targetTSH)->scsiID].SCSI_DF_disconnectEnable) &&
       (!hiob->SCSI_IF_disallowDisconnect))
   {
      scontrol |= SCSI_DISCENB;   /* Enable disconnect */
      /* We'll set tag to enable only if the disconnect was enable */
      if (hiob->SCSI_IF_tagEnable)
      {
         scontrol |= SCSIscontrol[iob->taskAttribute];

         /* Fill in the task_attribute field in case this is a 
          * packetized request. The task_attribute field of the 
          * SCB is automatically transferred by the ASIC to the
          * CMD IU when operating in packetized mode.  
          */
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_U320,
                                            task_attribute),
                                (SCSI_UEXACT8)
                                   SCSITaskAttribute[iob->taskAttribute]);
 
         /* Zero the task_management field in case this is a packetized
          * request.
          */
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_U320,
                                            task_management),
                                (SCSI_UEXACT8) 0);
      }
   }

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_U320,starget),
                          (SCSI_UEXACT8) targetTSH->scsiID);

#if SCSI_PACKETIZED_IO_SUPPORT
   /* Other LUN bytes must be zeroed for packetized requests. */
   HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,
                       scbBuffer,
                       OSMoffsetof(SCSI_SCB_STANDARD_U320,slun0),
                       adapterTSH->zeros, (SCSI_UEXACT8)SCSI_SU320_SLUN_LENGTH);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
   
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_U320,slun),
                          (SCSI_UEXACT8) slun);

   sg_cache_SCB = 2 * currentSGListSize;

   if (!iob->data.bufferSize)
   {
      /* indicate no data transfer */
      sg_cache_SCB |= SCSI_SU320_NODATA;
   }
   else
   {
      /* get segment length */
      HIM_GET_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,
                              &sgData,
                              iob->data.virtualAddress,
                              currentSGListSize);

      /* to handle one segment and zero in length passed in */
      if (sgData == (SCSI_UEXACT32)0x80000000)
      {
         /* indicate no data transfer */
         sg_cache_SCB |= SCSI_SU320_NODATA;
      }
      else
      {
         /* 1st s/g element should be done like this */
         /* setup the embedded s/g segment (the first s/g segment) */
         if (sgData & (SCSI_UEXACT32)0x80000000)
         {
            sg_cache_SCB |= SCSI_SU320_ONESGSEG;
         }
         else
         {
            /* more than one s/g segments and need to setup s/g pointer */
#if SCSI_SIMULATION_SUPPORT
            /* The only reason for a separate version of this portion of 
             * code is to reduce the size of the macro expansion. 
             * Previously, the simulation compiler would fault during
             * macro expansion.
             */
            busAddress = iob->data.busAddress;
            SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_U320,
                                            SCSI_SU320_sg_pointer_SCB0),
                                busAddress);
#else
            SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_U320,
                                            SCSI_SU320_sg_pointer_SCB0),
                                iob->data.busAddress);
#endif /* SCSI_SIMULATION_SUPPORT */
         }

         /* write segment length */
         HIM_PUT_LITTLE_ENDIAN24(adapterTSH->osmAdapterContext,
                                 scbBuffer,
                                 OSMoffsetof(SCSI_SCB_STANDARD_U320,slength0),
                                 sgData);

#if (OSM_BUS_ADDRESS_SIZE == 64)
         /* write segment address */
         /* support 32-bit/64-bit S/G segment address at run-time */
         SCSI_hGETBUSADDRESSSGPAD(&adapterTSH->hhcb,
                                  &busAddress,
                                  iob->data.virtualAddress,
                                  0);
#else
         /* write segment address */
         SCSI_hGETBUSADDRESS(&adapterTSH->hhcb,
                             &busAddress,
                             iob->data.virtualAddress,
                             0);
#endif /* (OSM_BUS_ADDRESS_SIZE == 64) */

         SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_U320,saddress0),
                             busAddress);
      }    
   }

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_U320,sg_cache_SCB),
                          sg_cache_SCB);

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_U320,scdb_length),
                          (SCSI_UEXACT8) iob->targetCommandLength);

   if (iob->targetCommandLength <= (HIM_UINT16)SCSI_SU320_SCDB_SIZE)
   {
      /* cdb embedded in scb buffer */
      HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_scdb0),
                          iob->targetCommand,iob->targetCommandLength);
   }
   else
   {
      /* cdb pointer embedded in scb buffer */
      HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,
                          iobReserve->cdb,
                          0,
                          iob->targetCommand,
                          iob->targetCommandLength);
      busAddress = iob->iobReserve.busAddress;
      OSMxAdjustBusAddress(&busAddress,OSMoffsetof(SCSI_IOB_RESERVE,cdb[0]));
      SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_U320,SCSI_SU320_scdb0),
                          busAddress);
   }

#if SCSI_PARITY_PER_IOB
   if (hiob->SCSI_IF_parityEnable)
   {
      scontrol |= SCSI_SU320_ENPAR;
   }
#endif /* SCSI_PARITY_PER_IOB */

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_U320,scontrol),
                          scontrol);

   HIM_PUT_LITTLE_ENDIAN16(adapterTSH->osmAdapterContext,
                           scbBuffer,
                           OSMoffsetof(SCSI_SCB_STANDARD_U320,
                                       SCSI_SU320_array_site),
                           hiob->scbDescriptor->scbNumber);

#if SCSI_SIMULATION_SUPPORT
   /* The only reason for a separate version of this portion of 
    * code is to reduce the size of the macro expansion. 
    * Previously, the simulation compiler would fault during
    * macro expansion.
    */
   busAddress = hiob->scbDescriptor->queueNext->scbBuffer.busAddress;
   SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                       scbBuffer,
                       OSMoffsetof(SCSI_SCB_STANDARD_U320,
                                   SCSI_SU320_next_SCB_addr0),
                       busAddress);
#else
   SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                       scbBuffer,
                       OSMoffsetof(SCSI_SCB_STANDARD_U320,
                                   SCSI_SU320_next_SCB_addr0),
                       hiob->scbDescriptor->queueNext->scbBuffer.busAddress);
#endif /* SCSI_SIMULATION_SUPPORT */

   /* Sequencer spec requires that SCB_flags be initialized to 0. */
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_U320,SCB_flags),
                          (SCSI_UEXACT8)0x00);

#if SCSI_SELTO_PER_IOB
   /* Not implemented yet */
   if (hiob->seltoPeriod > 0)
   {                                                                        
      /* The following is based on the assumption that ALTSTIM is 
       * always set to '0' in the normal path. The value needed to
       * set STIM and ALTSTIM to the desired value via an XOR is
       * hence: (CURRENT xor DESIRED). We map the requested value
       * in milliseconds to the nearest value supported by the
       * hardware.
       */
      dv_control = (SCSI_UEXACT8) (hhcb->SCSI_HF_selTimeout << 3);
      
      if (hiob->seltoPeriod == 1)
      {
         dv_control ^= (SCSI_W160M_ALTSTIM | 0x18);
      }
      else if (hiob->seltoPeriod == 2)
      {
         dv_control ^= (SCSI_W160M_ALTSTIM | 0x10);
      }
      else if (hiob->seltoPeriod <= 4)
      {
         dv_control ^= (SCSI_W160M_ALTSTIM | 0x08);
      }
      else if (hiob->seltoPeriod <= 32)
      {
         dv_control ^= 0x18;
      }
      else if (hiob->seltoPeriod <= 64)
      {
         dv_control ^= 0x10;
      }
      else if (hiob->seltoPeriod <= 128)
      {
         dv_control ^= 0x08;
      }

      dv_control |= SCSI_U320_SELTOPERIOB_ENABLE;
   }
#endif /* SCSI_SELTO_PER_IOB */

#if SCSI_DOMAIN_VALIDATION
   if (hiob->SCSI_IF_dvIOB)
   {                          
      scontrol1 |= SCSI_SU320_DV_ENABLE;
   }
#endif /* SCSI_DOMAIN_VALIDATION */

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_U320,scontrol1),
                          scontrol1);

#if SCSI_RAID1
   if (hiob->SCSI_IF_raid1)
   {
      /* put the mirror_SCB, mirror_slun, and mirror_starget in primary SCB */
      HIM_PUT_LITTLE_ENDIAN16(adapterTSH->osmAdapterContext,
                              scbBuffer,
                              OSMoffsetof(SCSI_SCB_STANDARD_U320,mirror_SCB),
                              hiob->mirrorHiob->scbNumber);

     /* Use the relatedIob here, use mirror hiob only for error recovery and SCB management */
                              
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_U320,mirror_slun),
                             (SCSI_UEXACT8)SCSI_TRGTSH(iob->relatedIob->taskSetHandle)->lunID);

      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_U320,mirror_starget),
                             (SCSI_UEXACT8)SCSI_TRGTSH(iob->relatedIob->taskSetHandle)->scsiID);
   }
   else
   {
      /* NULL the mirror_SCB, to prevent the mirroring of the command by sequencer */
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_U320,mirror_SCB)+1,
                             SCSI_NULL_ENTRY);
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_U320,mirror_SCB),
                             0x00);
   }
#endif /* SCSI_RAID1 */

   /* flush the cache ready for dma */
   SCSI_FLUSH_CACHE(scbBuffer,SCSI_SU320_SIZE_SCB);
}
#endif /* SCSI_STANDARD_U320_MODE */

/*********************************************************************
*
*  OSDStandardEnhU320BuildSCB
*
*     Build scb buffer for standard enhanced Ultra 320 mode
*
*  Return Value:  void
*                  
*  Parameters:    hiob
*
*  Remarks:       This routine is defined for internal HIM reference
*
*********************************************************************/
#if SCSI_STANDARD_ENHANCED_U320_MODE
void OSDStandardEnhU320BuildSCB (SCSI_HIOB HIM_PTR hiob,
                                 HIM_IOB HIM_PTR iob,
                                 SCSI_TARGET_TSCB HIM_PTR targetTSH,
                                 SCSI_ADAPTER_TSCB HIM_PTR adapterTSH)
{
   SCSI_IOB_RESERVE HIM_PTR iobReserve =
                     (SCSI_IOB_RESERVE HIM_PTR)iob->iobReserve.virtualAddress;
   SCSI_SCB_STANDARD_ENH_U320 HIM_PTR scbBuffer =
      (SCSI_SCB_STANDARD_ENH_U320 HIM_PTR) hiob->scbDescriptor->scbBuffer.virtualAddress;
   SCSI_BUS_ADDRESS busAddress;
   SCSI_UEXACT32 sgData;
   SCSI_UEXACT8 sg_cache_SCB;
   SCSI_UEXACT8 scontrol;
   SCSI_UEXACT8 slun;
#if ((OSM_BUS_ADDRESS_SIZE == 64) || SCSI_SELTO_PER_IOB)
   SCSI_HHCB HIM_PTR hhcb = &SCSI_ADPTSH(adapterTSH)->hhcb;
#endif /* ((OSM_BUS_ADDRESS_SIZE == 64) || SCSI_SELTO_PER_IOB) */
   SCSI_UEXACT8 currentSGListSize = sizeof(SCSI_BUS_ADDRESS);
   SCSI_UEXACT8 scontrol1 = 0;                               

#if (OSM_BUS_ADDRESS_SIZE == 64)
   if (hhcb->SCSI_HF_SgBusAddress32)
   {
      currentSGListSize = 4;
   }
#endif /* (OSM_BUS_ADDRESS_SIZE == 64) */

   scontrol = 0;     /* Assume disconnect is disabled */
   slun = (SCSI_UEXACT8) targetTSH->lunID;
   if ((SCSI_DEVICE_TABLE(&adapterTSH->hhcb)[(SCSI_UEXACT8)SCSI_TRGTSH(targetTSH)->scsiID].SCSI_DF_disconnectEnable) &&
       (!hiob->SCSI_IF_disallowDisconnect))
   {
      scontrol |= SCSI_DISCENB;   /* Enable disconnect */
      /* We'll set tag to enable only if the disconnect was enable */
      if (hiob->SCSI_IF_tagEnable)
      {
         scontrol |= SCSIscontrol[iob->taskAttribute];

         /* Fill in the task_attribute field in case this is a 
          * packetized request. The task_attribute field of the 
          * SCB is automatically transferred by the ASIC to the
          * CMD IU when operating in packetized mode.  
          */
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                            task_attribute),
                                (SCSI_UEXACT8)
                                   SCSITaskAttribute[iob->taskAttribute]);
 
         /* Zero the task_management field in case this is a packetized
          * request.
          */
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                            task_management),
                                (SCSI_UEXACT8) 0);
      }
   }

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,starget),
                          (SCSI_UEXACT8) targetTSH->scsiID);
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,slun),
                          (SCSI_UEXACT8) slun);

   sg_cache_SCB = 2 * currentSGListSize;

   if (!iob->data.bufferSize)
   {
      /* indicate no data transfer */
      sg_cache_SCB |= SCSI_SEU320_NODATA;
   }
   else
   {
      /* get segment length */
      HIM_GET_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,
                              &sgData,
                              iob->data.virtualAddress,
                              currentSGListSize);

      /* to handle one segment and zero in length passed in */
      if (sgData == (SCSI_UEXACT32)0x80000000)
      {
         /* indicate no data transfer */
         sg_cache_SCB |= SCSI_SEU320_NODATA;
      }
      else
      {
         /* 1st s/g element should be done like this */
         /* setup the embedded s/g segment (the first s/g segment) */
         if (sgData & (SCSI_UEXACT32)0x80000000)
         {
            sg_cache_SCB |= SCSI_SEU320_ONESGSEG;
         }
         else
         {
            /* more than one s/g segments and need to setup s/g pointer */
#if SCSI_SIMULATION_SUPPORT
            /* The only reason for a separate version of this portion of 
             * code is to reduce the size of the macro expansion. 
             * Previously, the simulation compiler would fault during
             * macro expansion.
             */
            busAddress = iob->data.busAddress;
            SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                            SCSI_SEU320_sg_pointer_SCB0),
                                busAddress);
#else
            SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                            SCSI_SEU320_sg_pointer_SCB0),
                                iob->data.busAddress);
#endif /* SCSI_SIMULATION_SUPPORT */
         }

         /* write segment length */
         HIM_PUT_LITTLE_ENDIAN24(adapterTSH->osmAdapterContext,
                                 scbBuffer,
                                 OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,slength0),
                                 sgData);

#if (OSM_BUS_ADDRESS_SIZE == 64)
         /* write segment address */
         /* support 32-bit/64-bit S/G segment address at run-time */
         SCSI_hGETBUSADDRESSSGPAD(&adapterTSH->hhcb,
                                  &busAddress,
                                  iob->data.virtualAddress,
                                  0);
#else
         /* write segment address */
         SCSI_hGETBUSADDRESS(&adapterTSH->hhcb,
                             &busAddress,
                             iob->data.virtualAddress,
                             0);
#endif /* (OSM_BUS_ADDRESS_SIZE == 64) */

         SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,saddress0),
                             busAddress);
      }    
   }

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,sg_cache_SCB),
                          sg_cache_SCB);

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,scdb_length),
                          (SCSI_UEXACT8) iob->targetCommandLength);

   if (iob->targetCommandLength <= (HIM_UINT16)SCSI_SEU320_SCDB_SIZE)
   {
      /* cdb embedded in scb buffer */
      HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_scdb0),
                          iob->targetCommand,iob->targetCommandLength);
   }
   else
   {
      /* cdb pointer embedded in scb buffer */
      HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,
                          iobReserve->cdb,
                          0,
                          iob->targetCommand,
                          iob->targetCommandLength);
      busAddress = iob->iobReserve.busAddress;
      OSMxAdjustBusAddress(&busAddress,OSMoffsetof(SCSI_IOB_RESERVE,cdb[0]));
      SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_scdb0),
                          busAddress);
   }

#if SCSI_PARITY_PER_IOB
   if (hiob->SCSI_IF_parityEnable)
   {
      scontrol |= SCSI_SU320_ENPAR;
   }
#endif /* SCSI_PARITY_PER_IOB */

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,scontrol),
                          scontrol);

   HIM_PUT_LITTLE_ENDIAN16(adapterTSH->osmAdapterContext,
                           scbBuffer,
                           OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                       SCSI_SEU320_array_site),
                           hiob->scbDescriptor->scbNumber);

#if SCSI_SIMULATION_SUPPORT
   /* The only reason for a separate version of this portion of 
    * code is to reduce the size of the macro expansion. 
    * Previously, the simulation compiler would fault during
    * macro expansion.
    */
   busAddress = hiob->scbDescriptor->queueNext->scbBuffer.busAddress;
   SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                       scbBuffer,
                       OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                   SCSI_SEU320_next_SCB_addr0),
                       busAddress);
#else
   SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                       scbBuffer,
                       OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                   SCSI_SEU320_next_SCB_addr0),
                       hiob->scbDescriptor->queueNext->scbBuffer.busAddress);
#endif /* SCSI_SIMULATION_SUPPORT */

   /* Sequencer spec requires that SCB_flags be initialized to 0. */
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,SCB_flags),
                          (SCSI_UEXACT8)0x00);

#if SCSI_SELTO_PER_IOB
   /* Not implemented yet */
   if (hiob->seltoPeriod > 0)
   {                                                                        
      /* The following is based on the assumption that ALTSTIM is 
       * always set to '0' in the normal path. The value needed to
       * set STIM and ALTSTIM to the desired value via an XOR is
       * hence: (CURRENT xor DESIRED). We map the requested value
       * in milliseconds to the nearest value supported by the
       * hardware.
       */
      dv_control = (SCSI_UEXACT8) (hhcb->SCSI_HF_selTimeout << 3);
      
      if (hiob->seltoPeriod == 1)
      {
         dv_control ^= (SCSI_W160M_ALTSTIM | 0x18);
      }
      else if (hiob->seltoPeriod == 2)
      {
         dv_control ^= (SCSI_W160M_ALTSTIM | 0x10);
      }
      else if (hiob->seltoPeriod <= 4)
      {
         dv_control ^= (SCSI_W160M_ALTSTIM | 0x08);
      }
      else if (hiob->seltoPeriod <= 32)
      {
         dv_control ^= 0x18;
      }
      else if (hiob->seltoPeriod <= 64)
      {
         dv_control ^= 0x10;
      }
      else if (hiob->seltoPeriod <= 128)
      {
         dv_control ^= 0x08;
      }

      dv_control |= SCSI_U320_SELTOPERIOB_ENABLE;
   }
#endif /* SCSI_SELTO_PER_IOB */

#if SCSI_DOMAIN_VALIDATION
   if (hiob->SCSI_IF_dvIOB)
   {                          
      scontrol1 |= SCSI_SEU320_DV_ENABLE;
   }
#endif /* SCSI_DOMAIN_VALIDATION */

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,scontrol1),
                          scontrol1);

#if SCSI_RAID1
   if (hiob->SCSI_IF_raid1)
   {
      /* put the mirror_SCB, mirror_slun, and mirror_starget in primary SCB */
      HIM_PUT_LITTLE_ENDIAN16(adapterTSH->osmAdapterContext,
                              scbBuffer,
                              OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,mirror_SCB),
                              hiob->mirrorHiob->scbNumber);

     /* Use the relatedIob here, use mirror hiob only for error recovery and SCB management */
                              
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,mirror_slun),
                             (SCSI_UEXACT8)SCSI_TRGTSH(iob->relatedIob->taskSetHandle)->lunID);

      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,mirror_starget),
                             (SCSI_UEXACT8)SCSI_TRGTSH(iob->relatedIob->taskSetHandle)->scsiID);
   }
   else
   {
      /* NULL the mirror_SCB, to prevent the mirroring of the command by sequencer */
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,mirror_SCB)+1,
                             SCSI_NULL_ENTRY);
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,mirror_SCB),
                             0x00);
   }
#endif /* SCSI_RAID1 */

   /* flush the cache ready for dma */
   SCSI_FLUSH_CACHE(scbBuffer,SCSI_SU320_SIZE_SCB);
}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/*********************************************************************
*
*  OSDDchU320BuildSCB
*
*     Build scb buffer for DCH_SCSI core
*
*  Return Value:  void
*                  
*  Parameters:    hiob
*
*  Remarks:       This routine is not functionally. As development 
*                 work progresses this function will be changed
*                 altered to accept new SCB register accesses and
*                 new generic SG list formats.
*
*********************************************************************/
#if SCSI_DCH_U320_MODE 
void OSDDchU320BuildSCB (SCSI_HIOB HIM_PTR hiob,
                         HIM_IOB HIM_PTR iob,
                         SCSI_TARGET_TSCB HIM_PTR targetTSH,
                         SCSI_ADAPTER_TSCB HIM_PTR adapterTSH)
{
   SCSI_IOB_RESERVE HIM_PTR iobReserve =
                     (SCSI_IOB_RESERVE HIM_PTR)iob->iobReserve.virtualAddress;
   SCSI_SCB_DCH_U320 HIM_PTR scbBuffer =
      (SCSI_SCB_DCH_U320 HIM_PTR) hiob->scbDescriptor->scbBuffer.virtualAddress;
   SCSI_BUS_ADDRESS busAddress;
   SCSI_UEXACT32 sgData;
   SCSI_UEXACT32 sgCount;
   SCSI_UEXACT16 sg_cache_SCB;
   SCSI_UEXACT8 scontrol;
   SCSI_UEXACT8 slun;
#if (OSM_BUS_ADDRESS_SIZE == 64)
   /* should not occur */
   SCSI_UEXACT8 currentSGListSize = sizeof(SCSI_BUS_ADDRESS);
#else
   SCSI_UEXACT8 currentSGListSize = sizeof(SCSI_BUS_ADDRESS)*2;
#endif /* (OSM_BUS_ADDRESS_SIZE == 64) */
   SCSI_UEXACT8 scontrol1 = 0;                               
   SCSI_UEXACT32 offSet;
#if SCSI_SIMULATION_SUPPORT
   SCSI_UEXACT8 x;
#endif /* SCSI_SIMULATION_SUPPORT */

   scontrol = 0;     /* Assume disconnect is disabled */
   slun = (SCSI_UEXACT8) targetTSH->lunID;
   if ((SCSI_DEVICE_TABLE(&adapterTSH->hhcb)[(SCSI_UEXACT8)SCSI_TRGTSH(targetTSH)->scsiID].SCSI_DF_disconnectEnable) &&
       (!hiob->SCSI_IF_disallowDisconnect))
   {
      scontrol |= SCSI_DISCENB;   /* Enable disconnect */
      /* We'll set tag to enable only if the disconnect was enable */
      if (hiob->SCSI_IF_tagEnable)
      {
         scontrol |= SCSIscontrol[iob->taskAttribute];

         /* Fill in the task_attribute field in case this is a 
          * packetized request. The task_attribute field of the 
          * SCB is automatically transferred by the ASIC to the
          * CMD IU when operating in packetized mode.  
          */
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_DCH_U320,task_attribute),
                                (SCSI_UEXACT8)SCSITaskAttribute[iob->taskAttribute]);
 
         /* Zero the task_management field in case this is a packetized
          * request.
          */
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_DCH_U320,task_management),
                                (SCSI_UEXACT8) 0);
      }
   }

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,starget),
                          (SCSI_UEXACT8) targetTSH->scsiID);
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,slun),
                          (SCSI_UEXACT8) slun);

   sg_cache_SCB = ((2 * currentSGListSize) << 8);

   if (!iob->data.bufferSize)
   {
      /* indicate no data transfer */
      sg_cache_SCB |= SCSI_DCHU320_NODATA;
   }
   else
   {
      /* get segment length */
      HIM_GET_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,
                              &sgCount,
                              iob->data.virtualAddress,
                              currentSGListSize);
                              
      /* get segment length */
      HIM_GET_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,
                              &sgData,
                              iob->data.virtualAddress,
                              currentSGListSize + 4);

      /* to handle one segment and zero in length passed in */
      if ((sgCount == (SCSI_UEXACT32)0x00000000) &&
          (sgData & (SCSI_UEXACT32)0x40000000)) 
      {
         /* indicate no data transfer */
         sg_cache_SCB |= SCSI_DCHU320_NODATA;
      }
      else
      {
         /* add ASEL bits to sg_cache_SCB */
         sg_cache_SCB |= ((SCSI_UEXACT32)(sgData >> SCSI_DCHU320_3BYTE) 
                           & SCSI_DCHU320_ASEL_MASK);

         /* Is this the last and only sg element? */
         if (sgData & (SCSI_UEXACT32)0x40000000)
         {
            /* set one seg flag. */
            sg_cache_SCB |= SCSI_DCHU320_ONESGSEG;
         }
         else
         {
            /* more than one s/g segments and need to setup s/g pointer */
            busAddress = iob->data.busAddress;

            offSet =  OSMoffsetof(SCSI_SCB_DCH_U320,
                                  SCSI_DCHU320_sg_pointer_SCB0);
     
            HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                                   scbBuffer,
                                   offSet,
                                   busAddress);

            /* Pad upper half of 64 bit address with zeros  */
            HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                                   scbBuffer, 
                                   offSet+4, 
                                   0l);
         }

         /* write segment length has changed to 32 for RBI */
         HIM_PUT_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,
                                 scbBuffer,
                                 OSMoffsetof(SCSI_SCB_DCH_U320,slength0),
                                 sgCount);


         /* write segment address */
         offSet =  OSMoffsetof(SCSI_SCB_DCH_U320,
                               saddress0);
         /* get the 0-31 */
         SCSI_hGETBUSADDRESS(&adapterTSH->hhcb,
                             &busAddress,
                             iob->data.virtualAddress,
                             0);
     
         HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                                scbBuffer,
                                offSet,
                                busAddress);

         /* get the 32-63 */
         SCSI_hGETBUSADDRESS(&adapterTSH->hhcb,
                             &busAddress,
                             iob->data.virtualAddress,
                             4);

         HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                                scbBuffer, 
                                offSet+4, 
                                busAddress);
         
      }    
   }
   /* change to '16' for DCH SCSI */
   HIM_PUT_LITTLE_ENDIAN16(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,sg_cache_SCB),
                          sg_cache_SCB);

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,scdb_length),
                          (SCSI_UEXACT8) iob->targetCommandLength);

   if (iob->targetCommandLength <= (HIM_UINT16)SCSI_DCHU320_SCDB_SIZE)
   {
      /* cdb embedded in scb buffer */
      HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_scdb0),
                          iob->targetCommand,
                          iob->targetCommandLength);
   }
/* changes for DCH due to memory restrictions. */
   else
   {
      /* cdb pointer embedded in scb buffer */
      HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,
                          iobReserve->cdb,
                          0,
                          iob->targetCommand,
                          iob->targetCommandLength);
      busAddress = iob->iobReserve.busAddress;
      OSMxAdjustBusAddress(&busAddress,OSMoffsetof(SCSI_IOB_RESERVE,cdb[0]));

      offSet =  OSMoffsetof(SCSI_SCB_DCH_U320,
                            SCSI_DCHU320_sg_pointer_SCB0);
     
     
      HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                             scbBuffer,
                             offSet,
                             busAddress);

      /* This should never exceed 32 bits. Pad upper dword with zeros */                                
      HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                             scbBuffer, 
                             offSet+4, 
                             0l);

   }

#if SCSI_PARITY_PER_IOB
   if (hiob->SCSI_IF_parityEnable)
   {
      scontrol |= SCSI_DCHU320_ENPAR;
   }
#endif /* SCSI_PARITY_PER_IOB */

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,scontrol),
                          scontrol);

   HIM_PUT_LITTLE_ENDIAN16(adapterTSH->osmAdapterContext,
                           scbBuffer,
                           OSMoffsetof(SCSI_SCB_DCH_U320,
                                       SCSI_DCHU320_array_site),
                           hiob->scbDescriptor->scbNumber);

   busAddress = hiob->scbDescriptor->queueNext->scbBuffer.busAddress;
   
   offSet =  OSMoffsetof(SCSI_SCB_DCH_U320,
                         SCSI_DCHU320_next_SCB_addr0);
   
   HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                          scbBuffer,
                          offSet,
                          busAddress);
                          
   HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                          scbBuffer, 
                          offSet+4, 
                          0l);

   /* change to '16' for DCH SCSI */
   HIM_PUT_LITTLE_ENDIAN16(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,sg_cache_SCB),
                          sg_cache_SCB);

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,scdb_length),
                          (SCSI_UEXACT8) iob->targetCommandLength);

   if (iob->targetCommandLength <= (HIM_UINT16)SCSI_DCHU320_SCDB_SIZE)
   {
      /* cdb embedded in scb buffer */
      HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_scdb0),
                          iob->targetCommand,iob->targetCommandLength);
   }
   else
   {
      /* cdb pointer embedded in scb buffer */
      HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,
                          iobReserve->cdb,
                          0,
                          iob->targetCommand,
                          iob->targetCommandLength);
      busAddress = iob->iobReserve.busAddress;
      OSMxAdjustBusAddress(&busAddress,OSMoffsetof(SCSI_IOB_RESERVE,cdb[0]));
      SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,SCSI_DCHU320_scdb0),
                          busAddress);
   }

#if SCSI_PARITY_PER_IOB
   if (hiob->SCSI_IF_parityEnable)
   {
      scontrol |= SCSI_DCHU320_ENPAR;
   }
#endif /* SCSI_PARITY_PER_IOB */

   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,scontrol),
                          scontrol);

   HIM_PUT_LITTLE_ENDIAN16(adapterTSH->osmAdapterContext,
                           scbBuffer,
                           OSMoffsetof(SCSI_SCB_DCH_U320,
                                       SCSI_DCHU320_array_site),
                           hiob->scbDescriptor->scbNumber);

#if SCSI_SIMULATION_SUPPORT
   busAddress = hiob->scbDescriptor->queueNext->scbBuffer.busAddress;
   SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                       scbBuffer,
                       OSMoffsetof(SCSI_SCB_DCH_U320,
                                   SCSI_DCHU320_next_SCB_addr0),
                       busAddress);
#else

   SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                       scbBuffer,
                       OSMoffsetof(SCSI_SCB_DCH_U320,
                                   SCSI_DCHU320_next_SCB_addr0),
                       hiob->scbDescriptor->queueNext->scbBuffer.busAddress);
#endif /* SCSI_SIMULATION_SUPPORT */

   /* Sequencer spec requires that SCB_flags be initialized to 0. */
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,SCB_flags),
                          (SCSI_UEXACT8)0x00);

#if SCSI_SELTO_PER_IOB
   /* Not implemented yet */
   if (hiob->seltoPeriod > 0)
   {                                                                        
      /* The following is based on the assumption that ALTSTIM is 
       * always set to '0' in the normal path. The value needed to
       * set STIM and ALTSTIM to the desired value via an XOR is
       * hence: (CURRENT xor DESIRED). We map the requested value
       * in milliseconds to the nearest value supported by the
       * hardware.
       */
      dv_control = (SCSI_UEXACT8) (hhcb->SCSI_HF_selTimeout << 3);
      
      if (hiob->seltoPeriod == 1)
      {
         dv_control ^= (SCSI_W160M_ALTSTIM | 0x18);
      }
      else if (hiob->seltoPeriod == 2)
      {
         dv_control ^= (SCSI_W160M_ALTSTIM | 0x10);
      }
      else if (hiob->seltoPeriod <= 4)
      {
         dv_control ^= (SCSI_W160M_ALTSTIM | 0x08);
      }
      else if (hiob->seltoPeriod <= 32)
      {
         dv_control ^= 0x18;
      }
      else if (hiob->seltoPeriod <= 64)
      {
         dv_control ^= 0x10;
      }
      else if (hiob->seltoPeriod <= 128)
      {
         dv_control ^= 0x08;
      }

      dv_control |= SCSI_U320_SELTOPERIOB_ENABLE;
      
   }
   
#endif /* SCSI_SELTO_PER_IOB */

#if SCSI_DOMAIN_VALIDATION
   if (hiob->SCSI_IF_dvIOB)
   {                          
      scontrol1 |= SCSI_DCHU320_DV_ENABLE;
   }
#endif /* SCSI_DOMAIN_VALIDATION */
      
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,scontrol1),
                          scontrol1);

#if SCSI_RAID1
   if(hiob->SCSI_IF_raid1)
   {
      /* put the mirror_SCB, mirror_slun, and mirror_starget in primary SCB */
      HIM_PUT_LITTLE_ENDIAN16(adapterTSH->osmAdapterContext,
                              scbBuffer,
                              OSMoffsetof(SCSI_SCB_DCH_U320,mirror_SCB),
                              hiob->mirroredSCB );
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_DCH_U320,mirror_slun),
                             (SCSI_UEXACT8)SCSI_TRGTSH(iob->relatedIob->taskSetHandle)->lunID 
                             );

      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_DCH_U320,mirror_starget),
                             (SCSI_UEXACT8)SCSI_TRGTSH(iob->relatedIob->taskSetHandle)->scsiID

                             );
   }
   else
   {
      /* NULL the mirror_SCB, to prevent the mirroring of the command by sequencer */
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_DCH_U320,mirror_SCB)+1,
                             SCSI_NULL_ENTRY );
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_DCH_U320,mirror_SCB),
                             0x00 );
   }
#endif /* SCSI_RAID1 */

   /* flush the cache ready for dma */
   SCSI_FLUSH_CACHE(scbBuffer,SCSI_DCHU320_SIZE_SCB);
}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  OSDGetNVData
*
*     Adjust bus address
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 destination
*                 sourceOffset                
*                 length
*  Remarks:       This routine is defined for internal HIM reference
*
*********************************************************************/
SCSI_UINT8 OSDGetNVData (SCSI_HHCB SCSI_HPTR hhcb,
                         void HIM_PTR destination,
                         HIM_UINT32 sourceOffset,
                         HIM_UINT32 length)
{
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = SCSI_GETADP(hhcb);

   return(OSMxGetNVData(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,destination,0,length));
}

/*********************************************************************
*
*  OSDGetSGList
*
*     Get the Scatter/Gather List
*
*  Return Value:  SCSI_BUS_ADDRESS
*                  
*  Parameters:    hiob
*
*  Remarks:       This routine is defined for internal HIM reference
*
*********************************************************************/
SCSI_BUFFER_DESCRIPTOR HIM_PTR OSDGetSGList (SCSI_HIOB SCSI_IPTR hiob)
{
   HIM_IOB HIM_PTR iob = SCSI_GETIOB(hiob);

   return(&(iob->data));
}

/*********************************************************************
*
*  OSDCompleteHIOB
*
*     Process notmal command complete
*
*  Return Value:  void
*                  
*  Parameters:    hiob
*
*  Remarks:       This routine is defined for internal HIM reference
*
*********************************************************************/
void OSDCompleteHIOB (SCSI_HIOB SCSI_IPTR hiob)
{
   HIM_IOB HIM_PTR iob = SCSI_GETIOB(hiob);

   /* process normal command first */
   if (hiob->stat == SCSI_SCB_COMP)
   {
      /* process successful status */
      iob->taskStatus = HIM_IOB_GOOD;
   }
   else
   {
      /* translate error status */
      SCSIxTranslateError(iob,hiob);
   }

#if SCSI_RAID1
   if (hiob->SCSI_IF_raid1)
   {
      /* process normal command first */
      if (hiob->mirrorHiob->stat == SCSI_SCB_COMP)
      {
         /* process successful status */
         iob->relatedIob->taskStatus = HIM_IOB_GOOD;
      }
      else
      {
         /* translate error status */
         SCSIxTranslateError(iob->relatedIob,hiob->mirrorHiob);
      }
   }
#endif /* SCSI_RAID1 */
   iob->postRoutine(iob);
}

/*********************************************************************
*
*  OSDCompleteSpecialHIOB
*
*     Process special command complete
*
*  Return Value:  void
*                  
*  Parameters:    hiob
*
*  Remarks:       This routine is defined for internal HIM reference
*
*********************************************************************/
void OSDCompleteSpecialHIOB (SCSI_HIOB SCSI_IPTR hiob)
{
   HIM_IOB HIM_PTR iob = SCSI_GETIOB(hiob);
   SCSI_HHCB HIM_PTR hhcb;
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = SCSI_ADPTSH(iob->taskSetHandle);
   SCSI_UNIT_CONTROL SCSI_UPTR targetUnit; 

   /* process special command */
   switch(iob->function)
   {
      case HIM_RESET_BUS_OR_TARGET:
         /* Reset SCSI Bus Iob */
         if (SCSI_TRGTSH(iob->taskSetHandle)->typeTSCB == SCSI_TSCB_TYPE_ADAPTER)
         {
            if (iob->errorData)
            {
               /* Only store this information in the area pointed to */
               /* by the errorData field if the pointer is not NULL. */
               targetUnit =
                  (SCSI_UNIT_CONTROL SCSI_UPTR)SCSI_ACTIVE_TARGET(hiob);               

               if (targetUnit != SCSI_NULL_UNIT_CONTROL) 
               {
                  /* If an I/O was active at the time of the reset, the
                   * snsBuffer field of the hiob contains targetUnit of
                   * the active hiob at the time of the reset.
                   */
                  ((HIM_SPECIAL_ERROR_DATA HIM_PTR)(iob->errorData))->resetBus.activeTargetTSH =
                     (HIM_TASK_SET_HANDLE)SCSI_GETTRGT(targetUnit);
               }
               else
               {
                  /* No I/O active at the time of the reset - */
                  /* therefore zero field.                              */
                  ((HIM_SPECIAL_ERROR_DATA HIM_PTR)(iob->errorData))->resetBus.activeTargetTSH = 0;
               }
            }
         }

         if (hiob->stat == SCSI_SCB_COMP)
         {
            /* process successful status */
            iob->taskStatus = HIM_IOB_GOOD;
         }
         else
         {
            /* translate error status */
            SCSIxTranslateError(iob, hiob);
         }

         break;

      case HIM_ABORT_TASK:
      case HIM_ABORT_TASK_SET:
      case HIM_UNFREEZE_QUEUE:
      case HIM_RESET_HARDWARE:
      case HIM_TERMINATE_TASK:
      case HIM_LOGICAL_UNIT_RESET:
         if (hiob->stat == SCSI_SCB_COMP)
         {
            /* process successful status */
            iob->taskStatus = HIM_IOB_GOOD;
         }
         else
         {
            /* translate error status */
            SCSIxTranslateError(iob, hiob);
         }

         break;

      case HIM_PROTOCOL_AUTO_CONFIG:
         hhcb = &SCSI_ADPTSH(iob->taskSetHandle)->hhcb;
#if (SCSI_TARGET_OPERATION && (!SCSI_FAST_PAC))
         /* Only perform bus scan if initiator mode enabled */ 
         if (hhcb->SCSI_HF_initiatorMode)
#endif /* (SCSI_TARGET_OPERATION && (!SCSI_FAST_PAC)) */

#if SCSI_OEM1_SUPPORT
         /*Clear QAS disable for OEM1*/
         if (hhcb->OEMHardware == SCSI_OEMHARDWARE_OEM1)
         {
            hhcb->SCSI_HF_adapterQASDisable =0;
         }   
#endif /* SCSI_OEM1_SUPPORT */

#if !SCSI_FAST_PAC
         {
            SCSI_ADPTSH(iob->taskSetHandle)->iobProtocolAutoConfig = iob;
            if (SCSIxSetupBusScan(SCSI_ADPTSH(iob->taskSetHandle)))
            {
               /* need to perform bus scan after protocol auto config */
               SCSIxQueueBusScan(SCSI_ADPTSH(iob->taskSetHandle));
               /* don't post until procedure is done */
            }
            else
            {
               /* SCSIxSetupBusScan indicates that there is nothing to scan */
               /* We'll just post for PAC completion here & return.         */

               /* Clear resetSCSI flag after the PAC */
               hhcb->SCSI_HF_resetSCSI = 0;

               if (hhcb->SCSI_HF_expSupport)
               {
                  SCSI_DISABLE_EXP_STATUS(hhcb);
               }

               adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_GOOD;

               adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);

               /* If OSM is frozen then unfreeze the OSM because the OSMEvent */
               /* handling is complete */
               if (adapterTSH->SCSI_AF_osmFrozen)
               {
                  /* Set OSM is Unfrozen flag */
                  adapterTSH->SCSI_AF_osmFrozen = 0;
                  /* Call OSM with Unfreeze Event. */
                  OSMxEvent(adapterTSH->osmAdapterContext,HIM_EVENT_OSMUNFREEZE,(void HIM_PTR) 0);
               }

               return;
            }   
         }   
#if SCSI_TARGET_OPERATION
         else
#endif /* SCSI_TARGET_OPERATION */

#endif /* !SCSI_FAST_PAC */

#if (SCSI_TARGET_OPERATION || SCSI_FAST_PAC)
         {
            /* post protocol auto config back to osm */
            iob->taskStatus = HIM_IOB_GOOD;
            iob->postRoutine(iob);

            /* If OSM is frozen then unfreeze the OSM because the OSMEvent handling */
            /* is complete */
            if (adapterTSH->SCSI_AF_osmFrozen)
            {
               /* Set OSM is Unfrozen flag */
               adapterTSH->SCSI_AF_osmFrozen = 0;
               /* Call OSM with Unfreeze Event. */
               OSMxEvent(adapterTSH->osmAdapterContext,HIM_EVENT_OSMUNFREEZE,(void HIM_PTR) 0);
            }
         }
#endif /* (SCSI_TARGET_OPERATION || SCSI_FAST_PAC) */

         return;
   
#if SCSI_TARGET_OPERATION         
      case HIM_ESTABLISH_CONNECTION:
         if (hiob->haStat != SCSI_HOST_ABT_HOST)
         {
            /* Copy the command length and the nexusTSH from the HIOB */  
            iob->targetCommandLength = SCSI_CMD_LENGTH(hiob);

            iob->taskSetHandle = (HIM_TASK_SET_HANDLE) SCSI_NEXUS_UNIT(hiob);
         }

         /* Set request type to device command initially */
         iob->flagsIob.targetRequestType = HIM_REQUEST_TYPE_CMND;

         if (hiob->stat == SCSI_SCB_COMP)
         {
            /* process successful status */
            
            iob->taskStatus = HIM_IOB_GOOD;
         }
         else
         if (hiob->stat == SCSI_TASK_CMD_COMP)
         {
            /* Task management function */
            SCSIxTargetTaskManagementRequest(iob,hiob);
         }
         else
         {
            /* translate error status */
            SCSIxTranslateError(iob,hiob);
         }

         break;
                      
      case HIM_REESTABLISH_INTERMEDIATE:
      case HIM_REESTABLISH_AND_COMPLETE:
      case HIM_ABORT_NEXUS:
#if SCSI_MULTIPLEID_SUPPORT
      case HIM_ENABLE_ID:
      case HIM_DISABLE_ID:
#endif /* SCSI_MULTIPLEID_SUPPORT */          
         if (hiob->stat == SCSI_SCB_COMP)
         {
            /* process successful status */
            iob->taskStatus = HIM_IOB_GOOD;
         }
         else
         if (hiob->stat == SCSI_TASK_CMD_COMP)
         {
            /* Task management function */
            SCSIxTargetTaskManagementRequest(iob,hiob);
         }
         else
         {
            /* translate error status */
            SCSIxTranslateError(iob,hiob);
         }

         break;
#endif /* SCSI_TARGET_OPERATION */

      default:
         /* indicate iob invalid and post back immediately */
         iob->taskStatus = HIM_IOB_INVALID;
         break;
   }

   iob->postRoutine(iob);
}

/*********************************************************************
*
*  SCSIxTranslateError
*
*     Translate hiob error status to iob error status
*
*  Return Value:  none 
*                  
*  Parameters:    iob
*                 hiob
*
*  Remarks:
*
*********************************************************************/
void SCSIxTranslateError (HIM_IOB HIM_PTR iob,
                          SCSI_HIOB HIM_PTR hiob)
{
   /* interpret stat of hiob */ 
   switch(hiob->stat)
   {
      case SCSI_SCB_ERR:
         /* interpret haStat of hiob */
         switch(hiob->haStat)
         {
            case SCSI_HOST_SEL_TO:
               iob->taskStatus = HIM_IOB_NO_RESPONSE;
               break;

            case SCSI_HOST_BUS_FREE:
               iob->taskStatus = HIM_IOB_CONNECTION_FAILED;
               break;

            case SCSI_HOST_PHASE_ERR:
            case SCSI_HOST_HW_ERROR:    
               iob->taskStatus = HIM_IOB_HOST_ADAPTER_FAILURE;
               break;

            case SCSI_HOST_SNS_FAIL:
               iob->taskStatus = HIM_IOB_ERRORDATA_FAILED;
               break;

            case SCSI_HOST_DETECTED_ERR:
#if SCSI_CRC_NOTIFICATION
               iob->residual = hiob->residualLength;
#endif /* SCSI_CRC_NOTIFICATION */
#if SCSI_DATA_IN_RETRY_DETECTION
            case SCSI_HOST_READ_RETRY:
               /* a data-in I/O request (read) completed successfully */
               /* after target retry. E.g. an intermediate data       */
               /* transfer was terminated without a save data         */
               /* pointers message and the data was resent on the     */
               /* next reconnect.                                     */
#endif /* SCSI_DATA_IN_RETRY_DETECTION */
               iob->taskStatus = HIM_IOB_PARITY_ERROR;
               break;

            case SCSI_HOST_DU_DO:
            case SCSI_HOST_NO_STATUS:
               /* interpret trgStatus of hiob */
               switch(hiob->trgStatus)
               {
                  case SCSI_UNIT_CHECK:
                     if (iob->flagsIob.autoSense)
                     {
                        /* Set return status */
                        iob->taskStatus = HIM_IOB_ERRORDATA_VALID;

                        /* Get residual sense length */
                        iob->residualError = (HIM_UEXACT32) hiob->snsResidual;

                        if (iob->errorDataLength > SCSI_HIOB_MAX_SENSE_LENGTH)
                        {
                           iob->residualError += (iob->errorDataLength - SCSI_HIOB_MAX_SENSE_LENGTH);
                        }
                     }
                     else
                     {
                        iob->taskStatus = HIM_IOB_ERRORDATA_REQUIRED;
                     }

                     if (hiob->haStat == SCSI_HOST_DU_DO)
                     {
                        iob->residual = hiob->residualLength;
                     }

#if SCSI_PACKETIZED_IO_SUPPORT
                     if (hiob->SCSI_IF_tmfValid != 0)   
                     {
                        /* Interpret Packetized Failure code */
                        switch (hiob->tmfStatus)
                        {
                           case SCSI_PFC_NO_FAILURE:         /* No failure */
                              if (hiob->haStat == SCSI_HOST_DU_DO)
                              {
                                 /* iob->residual is already assigned */
                                 iob->taskStatus = HIM_IOB_DATA_OVERUNDERRUN;
                              }
                              else
                              {
                                 iob->taskStatus = HIM_IOB_GOOD;
                              }
                              break;

                           case SCSI_PFC_SPI_CMD_IU_FIELDS_INVALID:
                              /* SPI Command Information Unit fields  invalid */
                           case SCSI_PFC_TMF_FAILED:
                              /* Task management function failed */
                           case SCSI_PFC_INV_TYPE_CODE_IN_LQ:
                              /* Invalid type code received in SPI L_Q IU */
                           case SCSI_PFC_ILLEGAL_REQUEST_IN_LQ:
                              /* Illegal request received in SPI L_Q IU */
                              iob->taskStatus = HIM_IOB_REQUEST_FAILED;         
                              break;

                           case SCSI_PFC_TMF_NOT_SUPPORTED:
                              /* Task management function not support */
                              iob->taskStatus = HIM_IOB_UNSUPPORTED_REQUEST;         
                              break;

                           default:
                              /* Reserved code */
                              iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
                              break;
                        }

                        /* If transportSpecific defined then fill in protocolStatus
                         * field with Packetized Failure Code.
                         */
                        if (iob->transportSpecific)
                        {
                           ((HIM_TS_SCSI HIM_PTR)(iob->transportSpecific))->protocolStatus =
                             (HIM_UEXACT8) hiob->tmfStatus;
                        }
                     }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
                     break;

                  case SCSI_UNIT_BUSY:
                     iob->taskStatus = HIM_IOB_BUSY;
                     break;

                  case SCSI_UNIT_RESERV:
                     iob->taskStatus = HIM_IOB_TARGET_RESERVED;
                     break;

                  case SCSI_UNIT_QUEFULL:
                     iob->taskStatus = HIM_IOB_TASK_SET_FULL;
                     break;

                  case SCSI_UNIT_GOOD:
                  case SCSI_UNIT_MET:
                  case SCSI_UNIT_INTERMED:
                  case SCSI_UNIT_INTMED_GD:
                     if (hiob->haStat == SCSI_HOST_DU_DO)
                     {
                        iob->taskStatus = HIM_IOB_DATA_OVERUNDERRUN;
                        iob->residual = hiob->residualLength;
                     }
                     else
                     {
                        iob->taskStatus = HIM_IOB_GOOD;
                     }
                     break;

                  case SCSI_UNIT_ACA_ACTIVE:
                  case SCSI_UNIT_TASK_ABTED:
                  default:
                     /* should never come to here */
                     iob->taskStatus = HIM_IOB_TRANSPORT_SPECIFIC;
                     /* If transportSpecific defined then fill in protocolStatus
                      * SCSI status value.
                      */
                     if (iob->transportSpecific)
                     {
                        ((HIM_TS_SCSI HIM_PTR)(iob->transportSpecific))->protocolStatus =
                          (HIM_UEXACT8) hiob->trgStatus;
                     }
                     break;
               }
               break;

            case SCSI_HOST_TAG_REJ:
            case SCSI_HOST_ABT_FAIL:
            case SCSI_HOST_RST_HA:   
            case SCSI_HOST_RST_OTHER:
               /* not implemented error */
               /* should never come to here */
               iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
               break;

            case SCSI_HOST_ABT_NOT_FND:
               iob->taskStatus = HIM_IOB_ABORT_NOT_FOUND;
               break;

            case SCSI_HOST_ABT_CMDDONE:
               iob->taskStatus = HIM_IOB_ABORT_ALREADY_DONE;
               break;

            case SCSI_HOST_NOAVL_INDEX: 
            case SCSI_HOST_INV_LINK:
               /* not implemented error */
               /* should never come to here */
               iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
               break;

#if SCSI_TARGET_OPERATION
            case SCSI_INITIATOR_PARITY_MSG:     /* Initiator message parity message  */ 
               iob->taskStatus = HIM_IOB_INITIATOR_DETECTED_PARITY_ERROR;
               break;
                
            case SCSI_INITIATOR_PARITY_ERR_MSG: /* Initiator detected error message*/ 
               iob->taskStatus = HIM_IOB_INITIATOR_DETECTED_ERROR;
               break;        

            case SCSI_HOST_MSG_REJECT:          /* Initiator rejected a message which  */
                                                /* we expected to be OK.               */
               iob->taskStatus = HIM_IOB_INVALID_MESSAGE_REJECT;
               break; 
                                         
            case SCSI_INITIATOR_INVALID_MSG:    /* Invalid message recieved from       */
                                                /* initiator. After the HIM issued a   */
                                                /* Message Reject message in response  */
                                                /* to an unrecognized or unsupported   */
                                                /* message the initiator released the  */
                                                /* ATN signal.                         */ 
               iob->taskStatus = HIM_IOB_INVALID_MESSAGE_RCVD;
               break;
                            
            case SCSI_HOST_IDENT_RSVD_BITS:     /* Identify Message has reserved bits  */
               iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
               break;                           /* set. XLM should never get this      */
                                                /* error code. HWM uses this code to   */
                                                /* indicate that a reserved bit is set */
                                                /* in the Identify message and the     */
                                                /* adapter profile field               */
                                                /* AP_SCSI2_IdentifyMsgRsv is set to   */
                                                /* 0, indicating that the HWM is to    */
                                                /* issue a message reject. Only        */
                                                /* applies to SCSI2.                   */
            case SCSI_HOST_IDENT_LUNTAR_BIT:    /* Identify Message has the luntar bit */
               iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
               break;                           /* set. XLM should never get this      */
                                                /* error code. HWM uses this code to   */
                                                /* indicate that the luntar bit is set */
                                                /* in the Identify message and the     */
                                                /* adapter profile field               */
                                                /* AP_SCSI2_TargetRejectLuntar is set  */
                                                /* to 1, indicating that the HWM is to */
                                                /* issue a message reject. Only        */
                                                /* applies to SCSI2.                   */

            case SCSI_HOST_TRUNC_CMD:           /* Truncated SCSI Command              */
               iob->taskStatus = HIM_IOB_TARGETCOMMANDBUFFER_OVERRUN; 
               break;
#endif /* SCSI_TARGET_OPERATION */

            case SCSI_HOST_ABT_CHANNEL_FAILED:
               iob->taskStatus = HIM_IOB_ABORTED_CHANNEL_FAILED;
               break;

            case SCSI_HOST_PROTOCOL_ERROR:
               iob->taskStatus = HIM_IOB_PROTOCOL_ERROR;
               break;

            default:
               /* should never come to here */
               iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
               break;
         }
         break;

      case SCSI_SCB_ABORTED:
         /* interpret haStat of hiob */
         switch(hiob->haStat)
         {
            case SCSI_HOST_ABT_HOST:
               iob->taskStatus = HIM_IOB_ABORTED_ON_REQUEST;
               break;

            case SCSI_HOST_ABT_HA:
               iob->taskStatus = HIM_IOB_ABORTED_CHIM_RESET;
               break;

            case SCSI_HOST_ABT_BUS_RST:
               iob->taskStatus = HIM_IOB_ABORTED_REQ_BUS_RESET;
               break;

            case SCSI_HOST_ABT_3RD_RST:
               iob->taskStatus = HIM_IOB_ABORTED_3RD_PARTY_RESET;
               break;

            case SCSI_HOST_ABT_IOERR:
               iob->taskStatus = HIM_IOB_ABORTED_TRANSPORT_MODE_CHANGE;
               break;

            case SCSI_HOST_ABT_TRG_RST:
               iob->taskStatus = HIM_IOB_ABORTED_REQ_TARGET_RESET;
               break;

            case SCSI_HOST_ABT_LUN_RST:
               iob->taskStatus = HIM_IOB_ABORTED_REQ_LUR;
               break;
               
            case SCSI_HOST_PHASE_ERR:
            case SCSI_HOST_HW_ERROR:    
               iob->taskStatus = HIM_IOB_HOST_ADAPTER_FAILURE;
               break;

#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
            /* PCIINT interrupt status for IOB */
            case SCSI_HOST_ABORTED_PCI_ERROR:
               iob->taskStatus = HIM_IOB_ABORTED_PCI_ERROR;
               break;

            /* SPLTINT interrupt status for IOB */
            case SCSI_HOST_ABORTED_PCIX_SPLIT_ERROR:
               iob->taskStatus = HIM_IOB_ABORTED_PCIX_SPLIT_ERROR;
               break;

            /* Interrupt indicates IOB active during PCI/PCI-X error */      
            case SCSI_HOST_PCI_OR_PCIX_ERROR:
               iob->taskStatus = HIM_IOB_PCI_OR_PCIX_ERROR;
               break;
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */

#if SCSI_TARGET_OPERATION
            /* Target mode HIOB Abort reasons */
            case SCSI_HOST_TARGET_RESET:
               iob->taskStatus = HIM_IOB_ABORTED_TR_RECVD;
               break;

            case SCSI_HOST_ABORT_TASK:
               iob->taskStatus = HIM_IOB_ABORTED_ABT_RECVD;
               break;

            case SCSI_HOST_ABORT_TASK_SET:
               iob->taskStatus = HIM_IOB_ABORTED_ABTS_RECVD;
               break;

            case SCSI_HOST_CLEAR_TASK_SET:
               iob->taskStatus = HIM_IOB_ABORTED_CTS_RECVD;
               break;

            case SCSI_HOST_TERMINATE_TASK:
               iob->taskStatus = HIM_IOB_ABORTED_TT_RECVD;
               break;

#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
            case SCSI_HOST_ABT_IU_REQ_CHANGE:
               iob->taskStatus = HIM_IOB_ABORTED_IU_REQ_CHANGE;
               break;
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */

#endif /* SCSI_TARGET_OPERATION */

            default:
               /* should never come to here */
               iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
               break;
         }
         break;

      case SCSI_SCB_PENDING:
         if (hiob->haStat == SCSI_HOST_ABT_STARTED)
         {
            iob->taskStatus = HIM_IOB_ABORT_STARTED;
         }
         else
         {
            iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
         }
         break;

      default:
         /* SCSI_SCB_COMP is not processed here */
         /* SCSI_TASK_CMD_COMP is not processed here */
         /* SCSI_SCB_INV_CMD should never happen for this implementation */
         /* we should never get to here */
         iob->taskStatus = HIM_IOB_UNDEFINED_TASKSTATUS;
         break;
   }
}

/*********************************************************************
*
*  OSDGetBusAddress
*
*     Get bus address
*
*  Return Value:  bus address
*                  
*  Parameters:    hhcb
*                 memory category
*                 virtual address
*
*  Remarks:       This routine is defined for internal HIM reference
*
*********************************************************************/
SCSI_BUS_ADDRESS OSDGetBusAddress (SCSI_HHCB SCSI_HPTR hhcb,
                                   SCSI_UEXACT8 category,
                                   void SCSI_IPTR virtualAddress)
{
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = SCSI_GETADP(hhcb);

   return(OSMxGetBusAddress(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,category,virtualAddress));   
}

/*********************************************************************
*
*  OSDAdjustBusAddress
*
*     Adjust bus address
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 bus address
*                 value for adjustment
*
*  Remarks:       This routine is defined for internal HIM reference
*
*********************************************************************/
void OSDAdjustBusAddress (SCSI_HHCB SCSI_HPTR hhcb,
                          SCSI_BUS_ADDRESS SCSI_IPTR busAddress,
                          SCSI_UINT value)
{
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = SCSI_GETADP(hhcb);

   OSMxAdjustBusAddress(busAddress,value);
}

/*********************************************************************
*
*  OSDDelay
*
*     Delays for the given number of microseconds
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 
*
*  Remarks:       This routine is defined for internal HIM reference
*
*********************************************************************/
void OSDDelay (SCSI_HHCB SCSI_HPTR hhcb, SCSI_UINT32 microSeconds)
{
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = SCSI_GETADP(hhcb);
   
   OSMxDelay(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,microSeconds);
}

/*********************************************************************
*
*  OSDReadPciConfiguration
*
*     Read dword value from pci configuration space
*
*  Return Value:  
*                  
*  Parameters:    hhcb
*                 registerNumber
*
*  Remarks:       This routine is defined for internal HIM reference
*
*********************************************************************/
#if !SCSI_DCH_U320_MODE 
SCSI_UEXACT32 OSDReadPciConfiguration (SCSI_HHCB SCSI_HPTR hhcb,
                                       SCSI_UEXACT8 registerNumber)
{
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = SCSI_GETADP(hhcb);

   return(OSMxReadPCIConfigurationDword(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,registerNumber));
}

/*********************************************************************
*
*  OSDWritePciConfiguration
*
*     Write dword value to pci configuration space
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 registerNumber
*                 value
*
*  Remarks:       This routine is defined for internal HIM reference
*
*********************************************************************/
void OSDWritePciConfiguration (SCSI_HHCB SCSI_HPTR hhcb,
                               SCSI_UEXACT8 regNumber,
                               SCSI_UEXACT32 value)
{
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = SCSI_GETADP(hhcb);

   OSMxWritePCIConfigurationDword(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,regNumber,value);
}
#endif /* !SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  OSDAsyncEvent
*
*     Handle asynch event from SCSI
*
*  Return Value:  none
*                  
*  Parameters:    hhcb
*                 event
*                 others...
*
*  Remarks:       This routine calls OSMEvent unless event is
*                 OSMUNFREEZE.
*
*********************************************************************/
void OSDAsyncEvent (SCSI_HHCB SCSI_HPTR hhcb,
                    SCSI_UINT16 event, ...)
{
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = SCSI_GETADP(hhcb);
   SCSI_UINT8 eventCHIM = 0;  /* Init to 0 to take care warning message */
#if SCSI_DOMAIN_VALIDATION
#if !SCSI_DV_REPORT_ALL_OSMEVENTS  
   SCSI_UEXACT8 suppressEventNotification = 0;
#endif /* !SCSI_DV_REPORT_ALL_OSMEVENTS */
   SCSI_HIOB SCSI_IPTR hiob;
#endif /* SCSI_DOMAIN_VALIDATION */

   /* translate the event per chim spec */
   switch(event)
   {
      case SCSI_AE_3PTY_RESET:
         /* If the bus could not be conditioned properly as a result
          * of the event then generate the IO_CHANNEL_FAILED event.
          */
         if (hhcb->SCSI_HF_busHung)
         {
            eventCHIM = HIM_EVENT_IO_CHANNEL_FAILED;
            /* Set badSeqCallerId to 'ignore callerId' value */
            hhcb->badSeqCallerId = (SCSI_UEXACT8)SCSI_BADSEQ_CALLID_NULL;
         }
         else
         {
            eventCHIM = HIM_EVENT_IO_CHANNEL_RESET;
         }
         break;

      /*case SCSI_AE_HAID_CHANGE: */ /* do nothing for now */

      case SCSI_AE_HA_RESET:
#if (SCSI_DOMAIN_VALIDATION && (!SCSI_DV_REPORT_ALL_OSMEVENTS))  
         if (adapterTSH->SCSI_AF_dvInProgress &&
             (hhcb->freezeEventReason == SCSI_AE_HA_RESET))
         {
            /* Suppress reported of event */
            suppressEventNotification = 1;
         }
#endif /* (SCSI_DOMAIN_VALIDATION && (!SCSI_DV_REPORT_ALL_OSMEVENTS)) */  

         /* If the bus could not be conditioned properly as a result
          * of the event then generate the IO_CHANNEL_FAILED event.
          */
         if (hhcb->SCSI_HF_busHung)
         {
            eventCHIM = HIM_EVENT_IO_CHANNEL_FAILED;
         }
         else
         {
            eventCHIM = HIM_EVENT_HA_FAILED;
         }
         break;

      case SCSI_AE_IOERROR:
         /* If the bus could not be conditioned properly as a result
          * of the event then generate the IO_CHANNEL_FAILED event.
          */
         if (hhcb->SCSI_HF_busHung)
         {
            /* Set badSeqCallerId to 'ignore callerId' value */
            hhcb->badSeqCallerId = (SCSI_UEXACT8)SCSI_BADSEQ_CALLID_NULL;
            eventCHIM = HIM_EVENT_IO_CHANNEL_FAILED;
         }
         else
         {
            eventCHIM = HIM_EVENT_TRANSPORT_MODE_CHANGE;
         }
         break;

#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING

      case SCSI_AE_ABORTED_PCI_ERROR:               /* PCI error detected  */
         eventCHIM = HIM_EVENT_PCI_ERROR;   
         break;

      case SCSI_AE_ABORTED_PCIX_SPLIT_ERROR:        /* PCI-X Split error detected */
         eventCHIM = HIM_EVENT_PCIX_SPLIT_ERROR;
         break;

#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */

      case SCSI_AE_OSMFREEZE:
#if SCSI_DOMAIN_VALIDATION
         if (adapterTSH->SCSI_AF_dvInProgress)
         {
            /* Freeze the DV process until unfreeze received. */
            adapterTSH->SCSI_AF_dvFrozen = 1;

            /* See if there is a DV request in the Done Q. If there is
             * then the event processing will not abort it as only the
             * Active entries are aborted.
             * Therefore, as the event was not a result of the DV
             * request we must force the DV to terminate. This is
             * achieved by assigning the event abort reason to the
             * taskStatus. However, the DV generated HA_RESET event
             * puts the I/O in the DONE Q before calling the event.
             * Therefore, this I/O must be excluded from this test
             * somehow. For now just use the haStat returned for
             * that case. Should be revisited.  
             */
            hiob = SCSI_SEARCH_DONEQ_FOR_DV_REQUEST(hhcb);
            if ((hiob != SCSI_NULL_HIOB) &&
                (hiob->haStat != SCSI_HOST_DU_DO))
            {
               if (hhcb->freezeEventReason == SCSI_AE_3PTY_RESET)
               {
                  hiob->stat = SCSI_SCB_ABORTED;
                  hiob->haStat = SCSI_HOST_ABT_3RD_RST;
                  /* Terminate DV now */
                  adapterTSH->SCSI_AF_dvInProgress = 0;
               }
               else
               if (hhcb->freezeEventReason == SCSI_AE_HA_RESET)
               {
                  hiob->stat = SCSI_SCB_ABORTED;
                  hiob->haStat = SCSI_HOST_PHASE_ERR;
                  /* Terminate DV now */
                  adapterTSH->SCSI_AF_dvInProgress = 0;  
               }
               else
               if (hhcb->freezeEventReason == SCSI_AE_IOERROR)
               {
                  hiob->stat = SCSI_SCB_ABORTED;
                  hiob->haStat = SCSI_HOST_ABT_IOERR;
                  /* Terminate DV now */
                  adapterTSH->SCSI_AF_dvInProgress = 0;  
               }                  
#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
               else
               if (hhcb->freezeEventReason == SCSI_AE_ABORTED_PCI_ERROR)
               {
                  hiob->stat = SCSI_SCB_ABORTED;
                  hiob->haStat = SCSI_HOST_ABORTED_PCI_ERROR;
                  /* Terminate DV now */
                  adapterTSH->SCSI_AF_dvInProgress = 0;  
               }
               else
               if (hhcb->freezeEventReason == SCSI_AE_ABORTED_PCIX_SPLIT_ERROR)
               {
                  hiob->stat = SCSI_SCB_ABORTED;
                  hiob->haStat = SCSI_HOST_ABORTED_PCIX_SPLIT_ERROR;
                  /* Terminate DV now */
                  adapterTSH->SCSI_AF_dvInProgress = 0;  
               }
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */
            }
#if !SCSI_DV_REPORT_ALL_OSMEVENTS 
            else
            if (hhcb->freezeEventReason == SCSI_AE_HA_RESET)
            {
               /* Suppress reported of event. */
               /* Only HA_RESET is suppressed all other 
                * events are fatal to the DV process.
                */
               suppressEventNotification = 1;
            }
#endif /* !SCSI_DV_REPORT_ALL_OSMEVENTS */
         }
#endif /* SCSI_DOMAIN_VALIDATION */

#if (SCSI_DOMAIN_VALIDATION && (!SCSI_DV_REPORT_ALL_OSMEVENTS))  
         if (!suppressEventNotification)
         {
            adapterTSH->SCSI_AF_osmFrozen = 1;   /* Set OSM is Frozen flag */
         }
#else
         adapterTSH->SCSI_AF_osmFrozen = 1;   /* Set OSM is Frozen flag */
#endif /* (SCSI_DOMAIN_VALIDATION && (!SCSI_DV_REPORT_ALL_OSMEVENTS)) */ 
         eventCHIM = HIM_EVENT_OSMFREEZE;
         break;

      case SCSI_AE_OSMUNFREEZE:
         /* 
          * OSMUNFREEZE will go to the OSM at the end of 
          * protocol auto config.
          */
#if SCSI_DOMAIN_VALIDATION
         /* Always clear the dvFrozen flag when unfreeze received
          * as DV may not continue.
          */
         adapterTSH->SCSI_AF_dvFrozen = 0;
         if (adapterTSH->SCSI_AF_dvInProgress)
         {
            /* DV process was suspended - need to continue. */
            SCSIxQueueDomainValidation(adapterTSH);
         }
#endif /* SCSI_DOMAIN_VALIDATION */
         /* Unfreeze events are reported through PAC. */
         return;

#if SCSI_TARGET_OPERATION
      case SCSI_AE_NEXUS_TSH_THRESHOLD:
         eventCHIM = HIM_EVENT_NEXUSTSH_THRESHOLD;  
         break;

      case SCSI_AE_EC_IOB_THRESHOLD:
         eventCHIM = HIM_EVENT_EC_IOB_THRESHOLD;
         break;
#endif /* SCSI_TARGET_OPERATION */

      case SCSI_AE_IO_CHANNEL_FAILED:
         eventCHIM = HIM_EVENT_IO_CHANNEL_FAILED;
         break;

   }

   if (eventCHIM == HIM_EVENT_IO_CHANNEL_FAILED)
   {
      /* reset bus hung indicator */
      SCSI_hSETBUSHUNG(hhcb,0);      
   }

#if (SCSI_DOMAIN_VALIDATION && (!SCSI_DV_REPORT_ALL_OSMEVENTS))
   if (!suppressEventNotification)
#endif /* (SCSI_DOMAIN_VALIDATION && (!SCSI_DV_REPORT_ALL_OSMEVENTS)) */
   {
      /* inform the OSM about the event */
      if ((eventCHIM == HIM_EVENT_IO_CHANNEL_FAILED) || 
          (eventCHIM == HIM_EVENT_HA_FAILED))
      {
#if SCSI_REPORT_OSMEVENT_INFO
         OSMxEvent(adapterTSH->osmAdapterContext,
                   eventCHIM,
                   (void HIM_PTR) 0,
                   (HIM_UINT32)hhcb->badSeqCallerId);
#else
#if !SCSI_DCH_U320_MODE
         OSMDebugReportEventInfo(adapterTSH->osmAdapterContext,
                                 (HIM_UINT32)hhcb->badSeqCallerId);
#endif /* !SCSI_DCH_U320_MODE */
         OSMxEvent(adapterTSH->osmAdapterContext,
                   eventCHIM,
                   (void HIM_PTR) 0);
#endif /* SCSI_REPORT_OSMEVENT_INFO */
      }
      else
      {
         OSMxEvent(adapterTSH->osmAdapterContext,
                   eventCHIM,
                   (void HIM_PTR) 0);
      }
   }
}

/*********************************************************************
*
*  OSDGetHostAddress
*
*     Get address of the host device
*
*  Return Value:  pointer to host address
*                  
*  Parameters:    hhcb
*
*  Remarks:       The host address aquired from this routine can
*                 be used to either group the host devices or
*                 select scb memory bank (Excalibur)
*
*********************************************************************/
SCSI_HOST_ADDRESS SCSI_LPTR OSDGetHostAddress (SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = SCSI_GETADP(hhcb);

   return((SCSI_HOST_ADDRESS HIM_PTR)&(adapterTSH->hostAddress));
}

/*********************************************************************
*
*  OSDInExact8High
*
*     Read an 8 bit quantity from the "upper" register bank of the
*     ASIC.
*
*  Return Value:  contents of adapter register  
*                  
*  Parameters:    scsiRegister - register handle
*                 reg          - register to be read 
*
*  Remarks:       The contents of the register specified by the reg
*                 parameter is returned. The register access is 
*                 dependent on the access type (memory or I/O mapped).
*
*********************************************************************/
SCSI_UEXACT8 OSDInExact8High (SCSI_REGISTER scsiRegister,
                              SCSI_UINT32 reg)
{

   if (scsiRegister->memoryMapped == (HIM_UINT8)HIM_MEMORYSPACE)
   {
      /* Memory mapped - use memory handle */
      return ((SCSI_UEXACT8) scsiRegister->OSMReadUExact8(
                               scsiRegister->ioHandle,
                               (HIM_UINT32)(reg)));
   }
   else
   {
      /* I/O mapped - use 2nd I/O handle and adjust reg value */
      return((SCSI_UEXACT8) scsiRegister->OSMReadUExact8(
                               scsiRegister->ioHandleHigh,
                               (HIM_UINT32)(reg)-(HIM_UINT32)SCSI_HIGHREGBANK_START));
   }
}

/*********************************************************************
*
*  OSDOutExact8High
*
*     Write an 8 bit quantity to the "upper" register bank of the ASIC.
*
*  Return Value:  none
*               
*                  
*  Parameters:    scsiRegister - register handle
*                 reg          - register to be written 
*                 value        - value to be written
*
*  Remarks:       The host address aquired from this routine can
*                 be used to either group the host devices or
*                 select scb memory bank (Excalibur)
*
*********************************************************************/
void OSDOutExact8High (SCSI_REGISTER scsiRegister,
                       SCSI_UINT32 reg,
                       SCSI_UEXACT8 value)
{
   if (scsiRegister->memoryMapped == (HIM_UINT8)HIM_MEMORYSPACE)
   {
      /* Memory mapped - use memory handle */
      scsiRegister->OSMWriteUExact8(scsiRegister->ioHandle,
                                    (HIM_UINT32)(reg),
                                    (HIM_UEXACT8)(value));
   }
   else
   {
      /* I/O mapped - use 2nd I/O handle and adjust reg value */
      scsiRegister->OSMWriteUExact8(scsiRegister->ioHandleHigh,
                                    ((HIM_UINT32)(reg)-(HIM_UINT32)SCSI_HIGHREGBANK_START),
                                    (HIM_UEXACT8)(value));
   }
}

/*********************************************************************
*
*  OSDInExact16High
*
*     Read an 16 bit quantity from the "upper" register bank of the
*     ASIC.
*
*  Return Value:  contents of adapter register  
*                  
*  Parameters:    scsiRegister - register handle
*                 reg          - register to be read 
*
*  Remarks:       The contents of the register specified by the reg
*                 parameter is returned. The register access is 
*                 dependent on the access type (memory or I/O mapped).
*
*********************************************************************/
#if SCSI_DCH_U320_MODE
SCSI_UEXACT16 OSDInExact16High (SCSI_REGISTER scsiRegister,
                                SCSI_UINT32 reg)
{

   if (scsiRegister->memoryMapped == (HIM_UINT8)HIM_MEMORYSPACE)
   {
      /* Memory mapped - use memory handle */
      return ((SCSI_UEXACT16) scsiRegister->OSMReadUExact16(
                               scsiRegister->ioHandle,
                               (HIM_UINT32)(reg)));
   }
   else
   {
      /* I/O mapped - use 2nd I/O handle and adjust reg value */
      return((SCSI_UEXACT16) scsiRegister->OSMReadUExact16(
                               scsiRegister->ioHandleHigh,
                               (HIM_UINT32)(reg)-(HIM_UINT32)SCSI_HIGHREGBANK_START));
   }
}

/*********************************************************************
*
*  OSDOutExact16High
*
*     Write an 16 bit quantity to the "upper" register bank of the ASIC.
*
*  Return Value:  none
*               
*                  
*  Parameters:    scsiRegister - register handle
*                 reg          - register to be written 
*                 value        - value to be written
*
*  Remarks:       The host address aquired from this routine can
*                 be used to either group the host devices or
*                 select scb memory bank (Excalibur)
*
*********************************************************************/
void OSDOutExact16High (SCSI_REGISTER scsiRegister,
                        SCSI_UINT32 reg,
                        SCSI_UEXACT16 value)
{
   if (scsiRegister->memoryMapped == (HIM_UINT8)HIM_MEMORYSPACE)
   {
      /* Memory mapped - use memory handle */
      scsiRegister->OSMWriteUExact16(scsiRegister->ioHandle,
                                     (HIM_UINT32)(reg),
                                     (HIM_UEXACT16)(value));
   }
   else
   {
      /* I/O mapped - use 2nd I/O handle and adjust reg value */
      scsiRegister->OSMWriteUExact16(scsiRegister->ioHandleHigh,
                                     ((HIM_UINT32)(reg)-(HIM_UINT32)SCSI_HIGHREGBANK_START),
                                     (HIM_UEXACT16)(value));
   }
}
#endif /* SCSI_DCH_U320_MODE */

/*********************************************************************
*
*  OSDSynchronizeRange
*
*     Synchronize Register Range(s).
*
*  Return Value:  none
*               
*                  
*  Parameters:    scsiRegister - register handle
*
*  Remarks:       This routine calls the OSMSynchronizeRange 
*                 function to ensure previous OSMRead/OSMWrite
*                 operations have completed. If the adapter is 
*                 using I/O mapping then a call for each (both)
*                 iohandle ranges is made.
*
*********************************************************************/
void OSDSynchronizeRange (SCSI_REGISTER scsiRegister)
{

   if (scsiRegister->memoryMapped == (HIM_UINT8)HIM_MEMORYSPACE)
   {
      /* Memory mapped I/O. */ 
      scsiRegister->OSMSynchronizeRange(scsiRegister->ioHandle,
                       (HIM_UINT32)SCSI_IOHANDLE_OFFSET,
                       (HIM_UINT32)SCSI_MEMMAPPED_HANDLE_LENGTH);
   }
   else
   {
      /* I/O mapped - need to synchronize both I/O handles */
      scsiRegister->OSMSynchronizeRange(scsiRegister->ioHandle,
                       (HIM_UINT32)SCSI_IOHANDLE_OFFSET,
                       (HIM_UINT32)SCSI_IOMAPPED_HANDLE_LENGTH);
      scsiRegister->OSMSynchronizeRange(scsiRegister->ioHandleHigh,
                       (HIM_UINT32)SCSI_IOHANDLE_OFFSET,
                       (HIM_UINT32)SCSI_IOMAPPED_HANDLE_LENGTH);
   }
}

/*********************************************************************
*
*  SCSIxGetBiosInformation
*
*     Get bios information
*
*  Return Value:  none
*                  
*  Parameters:    adapterTSH
*
*  Remarks:
*
*********************************************************************/
#if SCSI_BIOS_ASPI8DOS
void SCSIxGetBiosInformation (HIM_TASK_SET_HANDLE adapterTSH)
{
   SCSI_HHCB HIM_PTR hhcb = &SCSI_ADPTSH(adapterTSH)->hhcb;
   SCSI_BIOS_INFORMATION HIM_PTR biosInformation =
                                 &SCSI_ADPTSH(adapterTSH)->biosInformation;
   SCSI_SCBFF biosScbFF;
   SCSI_UINT8 i;
   SCSI_UEXACT8 biosSignature[4];
   SCSI_UEXACT8 biosVersion[2];
         
   /* check if bios exist */
   if (SCSI_BIOS_STATE(hhcb) == SCSI_BIOS_ACTIVE)
   {
      /* Load scb ptr with 0xFF */
      SCSI_READ_SCB_RAM(hhcb,
                        (SCSI_UEXACT8 SCSI_SPTR) &biosScbFF,
                        (SCSI_UEXACT16) SCSI_U320_SCBFF_PTR,
                        (SCSI_UEXACT8)0,
                        (SCSI_UEXACT8)SCSI_SCBFF_SIZE);

      biosInformation->biosFlags.biosActive = 1;
      biosInformation->numberDrives = 0;
      biosInformation->biosFlags.extendedTranslation = 0;
      biosInformation->firstBiosDrive = biosInformation->lastBiosDrive = (SCSI_UINT8)0xFF;
      
      for (i = 0; i < SCSI_MAXDEV; i++)
      {
         if (biosScbFF.int13Drive[i] & SCSI_INT13DRIVE_VALID_FLAG)
         {

            /* load drive scsi/lun id */
            biosInformation->biosDrive[i].targetID = (SCSI_UINT8) 
                  (biosScbFF.int13Drive[i] & SCSI_INT13DRIVE_ID_MASK);
            biosInformation->biosDrive[i].lunID    = (SCSI_UINT8) 
                  (biosScbFF.int13Drive[i] & SCSI_INT13DRIVE_LUN_MASK) >> 4;

            /* update first drive, last drive, total drives & */
            /* extended translation */
            if (biosInformation->firstBiosDrive == 0xFF)
            {
              biosInformation->firstBiosDrive = i;
            }
            biosInformation->lastBiosDrive = i;
            biosInformation->numberDrives += 1;

            /* Update extendedTranslation flag if one of */
            /* the int13Drive's >1GB bit is set.         */
            if (biosScbFF.int13Drive[i] & SCSI_INT13DRIVE_1GB)
            {
               biosInformation->biosFlags.extendedTranslation = 1;
            }
         }                                   
      }     
   }
   SCSI_READ_SCB_RAM(hhcb,
                     (SCSI_UEXACT8 SCSI_SPTR) &biosSignature[0],
                     (SCSI_UEXACT16)SCSI_U320_SCBFF_PTR, 
                     (SCSI_UEXACT8) SCSI_SCBFF_BIOS_SIGNATURE_OFST,
                     (SCSI_UEXACT8) 4);
   if ((biosSignature[0] == 'B') && (biosSignature[1] == 'I') &&
       (biosSignature[2] == 'O') && (biosSignature[3] == 'S'))
   {
      biosInformation->versionFormat = HIM_BV_FORMAT1;

      SCSI_READ_SCB_RAM(hhcb,
                        (SCSI_UEXACT8 SCSI_SPTR) &biosVersion[0],
                        (SCSI_UEXACT16)SCSI_U320_SCBFF_PTR, 
                        (SCSI_UEXACT8) SCSI_SCBFF_BIOS_VERSION_OFST,
                        (SCSI_UEXACT8) 2);
      biosInformation->majorNumber = (SCSI_UINT8)((biosVersion[1] & 0xF0) >> 4);
      biosInformation->minorNumber = (SCSI_UINT8)(((biosVersion[1] & 0x0F) << 4) | ((biosVersion[0] & 0xF0) >> 4));
      biosInformation->subMinorNumber = (SCSI_UINT8)(biosVersion[0] & 0x0F);
   }
   else
   {
      biosInformation->versionFormat = HIM_BV_FORMAT_UNKNOWN;
   }
}
#endif /* SCSI_BIOS_ASPI8DOS */

/*********************************************************************
*
*  SCSIxChkLunExist
*
*     Check if the specified lun exist in the lun/tsk exist table
*
*  Return Value:  1 - lun/target exist
*                 0 - not exist
*                  
*  Parameters:    lunExist table
*                 target id
*                 lun id
*
*  Remarks:
*
*********************************************************************/
SCSI_UINT8 SCSIxChkLunExist (SCSI_UEXACT8 HIM_PTR lunExist,
                             SCSI_UINT8 target,
                             SCSI_UINT8 lun)
{
   SCSI_UINT index = (SCSI_UINT)(target * SCSI_MAXLUN + lun);

   return((SCSI_UINT8)((lunExist[index/8] & (1 << (index % 8))) != 0));
}

/*********************************************************************
*
*  SCSIxSetLunExist
*
*     Set the lun exist in lun/tsh exist table
*
*  Return Value:  none
*                  
*  Parameters:    lunExist table
*                 target id
*                 lun id
*
*  Remarks:
*
*********************************************************************/
void SCSIxSetLunExist (SCSI_UEXACT8 HIM_PTR lunExist,
                       SCSI_UINT8 target,
                       SCSI_UINT8 lun)
{
   SCSI_UINT index = (SCSI_UINT)(target * SCSI_MAXLUN + lun);

   lunExist[index/8] |= (SCSI_UEXACT8) (1 << (index % 8));
}

/*********************************************************************
*
*  SCSIxClearLunExist
*
*     Clear the lun exist in lun/tsh exist table
*
*  Return Value:  none
*                  
*  Parameters:    lunExist table
*                 target id
*                 lun id
*
*  Remarks:
*
*********************************************************************/
void SCSIxClearLunExist (SCSI_UEXACT8 HIM_PTR lunExist,
                         SCSI_UINT8 target,
                         SCSI_UINT8 lun)
{
   SCSI_UINT index = (SCSI_UINT)(target * SCSI_MAXLUN + lun);

   lunExist[index/8] &= (SCSI_UEXACT8)(~(1 << (index % 8)));
}

/*********************************************************************
*
*  SCSIxSetupBusScan
*
*     Setup for SCSI bus scan
*
*  Return Value:  0 - All targets are marked with 0 luns.
*                     No need to call SCSIxQueueBusScan.
*                 1 - It is ok to proceed with SCSIxQueueBusScan.
*
*  Parameters:    adapterTSH
*
*  Remarks:
*
*********************************************************************/
SCSI_UINT8 SCSIxSetupBusScan (SCSI_ADAPTER_TSCB HIM_PTR adapterTSH)
{
   SCSI_TARGET_TSCB HIM_PTR targetTSCB = &adapterTSH->targetTSCBBusScan;
   SCSI_HHCB SCSI_HPTR hhcb = &adapterTSH->hhcb;
   SCSI_UINT8 i;

   /* fresh the lunExist table */
   OSDmemset(adapterTSH->lunExist,0,SCSI_MAXDEV*SCSI_MAXLUN/8);

   /* fresh the bus scan state machine (start with target 0, lun 0) */
   OSDmemset(targetTSCB,0,sizeof(SCSI_TARGET_TSCB));

   /* setup targetTSCB */
   targetTSCB->typeTSCB   = SCSI_TSCB_TYPE_TARGET;
   targetTSCB->adapterTSH = adapterTSH;

   /* The following loop skip target(s) with NumberLuns == 0   */
   for (;targetTSCB->scsiID < adapterTSH->hhcb.maxDevices;)
   {
      if (adapterTSH->NumberLuns[targetTSCB->scsiID] == 0)
      {
         /* If NumberLuns specified for the target is 0, skip this target */
         ++targetTSCB->scsiID;
         continue;
      }

      if (targetTSCB->scsiID == adapterTSH->hhcb.hostScsiID)
      {
#if !SCSI_NULL_SELECTION
         /* skip the host scsi id */
         ++targetTSCB->scsiID;
         continue;
#else
         /* Don't skip host scsi id - attempt Null Selection */
         /* Can't disconnect on Null Selection */
         SCSI_DEVICE_TABLE(hhcb)[targetTSCB->scsiID].SCSI_DF_disconnectEnable = 0;
         break;
#endif /* !SCSI_NULL_SELECTION */
      }
      
      break;
   }

   if (targetTSCB->scsiID >= adapterTSH->hhcb.maxDevices)
   {
      /* This may happen if all targets are specified with NumberLuns==0 */
      /* For this case no actual scanning need to be done.               */
      return 0;
   }

   /* reset tag enable */
   adapterTSH->tagEnable = 0;

   adapterTSH->retryBusScan = 0;

   if (hhcb->SCSI_HF_expSupport)
   {
      /* invalidate the bus expander status */
      for (i = 0; i < SCSI_MAXDEV; i++)
      {
         hhcb->deviceTable[i].SCSI_DF_behindExp = 0;
      }
   }

#if SCSI_DOMAIN_VALIDATION
   for (i = 0; i < SCSI_MAXDEV; i++)
   {
      /* disable domain validation as default */
      adapterTSH->SCSI_AF_dvLevel(i) = SCSI_DV_DISABLE;
   }
#endif /* SCSI_DOMAIN_VALIDATION */

   /* This indicates that its ok to proceed with bus scan process */
   return 1;
}

/*********************************************************************
*
*  SCSIxSetupLunProbe
*
*     Setup to probe a LUN.
*
*  Return Value:  none
*                  
*  Parameters:    adapterTSH
*
*  Remarks:
*
*********************************************************************/
#if !SCSI_DISABLE_PROBE_SUPPORT
void SCSIxSetupLunProbe (SCSI_ADAPTER_TSCB HIM_PTR adapterTSH, 
                         SCSI_UEXACT8 target,
                         SCSI_UEXACT8 lun)
{
   SCSI_TARGET_TSCB HIM_PTR targetTSCB = &adapterTSH->targetTSCBBusScan;
#if SCSI_DOMAIN_VALIDATION
   SCSI_UINT16 i;
#endif /* SCSI_DOMAIN_VALIDATION */

   /* fresh the lunExist entry */
   SCSIxClearLunExist(adapterTSH->lunExist, (SCSI_UINT8)target, (SCSI_UINT8)lun);

   /* @dm nt5 - TSH invalid workaround for NT 5.0 start */
   /* This is a workaround for nt 5.0.  With NT 5.0, the targetTSH and TSCB is*/
   /* stored in the LU extension. The problem is that with NT 5.0, the OS can */
   /* clear the LU extension memory at any given time without warning.  If    */
   /* this occurs, then the OSM has no choice, but to probe for the target/lun*/
   /* for the new request when the LU extension clearing is detected.  If this*/
   /* occurs, then CHIM rules say that you cant probe for a device that has a */
   /* preexisting tsh.  This code is added to clear the tsh exist flag for the*/
   /* target/lun (in other words, dont error check this case) to workaround   */
   /* NT 5.0 problem.                                                         */
   /* fresh the lunExist entry */
   SCSIxClearLunExist(adapterTSH->tshExist, (SCSI_UINT8)target, (SCSI_UINT8)lun);
   /* @dm nt5 - TSH invalid workaround for NT 5.0 end */

   /* fresh the target TSCB */
   OSDmemset(targetTSCB,0,sizeof(SCSI_TARGET_TSCB));
   targetTSCB->scsiID = target;
   targetTSCB->lunID = lun;

   /* setup targetTSCB */
   targetTSCB->typeTSCB = SCSI_TSCB_TYPE_TARGET;
   targetTSCB->adapterTSH = adapterTSH;

   if (targetTSCB->lunID == 0)
   {
      /* reset tag enable */
      adapterTSH->tagEnable &= (SCSI_UEXACT16)(~(1 << targetTSCB->scsiID));
   }

   adapterTSH->retryBusScan = 0;

#if SCSI_DOMAIN_VALIDATION
   for (i = 0; i < SCSI_MAXDEV; i++)
   {
      /* disable domain validation as default */
      adapterTSH->SCSI_AF_dvLevel(i) = SCSI_DV_DISABLE;
   }
#endif /* SCSI_DOMAIN_VALIDATION */

#if SCSI_NULL_SELECTION
   if (targetTSCB->scsiID == adapterTSH->hhcb.hostScsiID)
   {
      /* Can't disconnect on Null Selection */
      SCSI_DEVICE_TABLE(&adapterTSH->hhcb)[targetTSCB->scsiID].SCSI_DF_disconnectEnable = 0;
   }  
#endif /* SCSI_NULL_SELECTION */

}
#endif /* !SCSI_DISABLE_PROBE_SUPPORT */

#if SCSI_PAC_SEND_SSU
/*********************************************************************
*
*  SCSIxQueueSSU
*
*     Queue Start/Stop Unit request. It is assumed that this routine
*     is called only during PAC (not HIM_PROBE). Also it is assumed
*     that this routine is called only for LUN 0.
*
*  Return Value:  none
*                  
*  Parameters:    adapterTSH
*
*  Remarks:
*
*********************************************************************/
void SCSIxQueueSSU (SCSI_ADAPTER_TSCB HIM_PTR adapterTSH)
{
   SCSI_TARGET_TSCB HIM_PTR targetTSCB = &adapterTSH->targetTSCBBusScan;
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   HIM_IOB HIM_PTR iob;
   HIM_UEXACT8 HIM_PTR uexact8Pointer;

   /* setup next iob request (inquiry) for bus scan */
   /* get iob memory */
   iob = (HIM_IOB HIM_PTR)(((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreUnlocked) + SCSI_MORE_IOB);
   OSDmemset(iob,0,sizeof(HIM_IOB));

   /* setup iob reserved memory */
   iob->iobReserve.virtualAddress = (void HIM_PTR) (((SCSI_UEXACT8 HIM_PTR)
               adapterTSH->moreLocked) + SCSI_MORE_IOBRSV);
   iob->iobReserve.bufferSize = sizeof(SCSI_IOB_RESERVE);
   iob->iobReserve.busAddress = OSMxGetBusAddress(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                  HIM_MC_LOCKED,iob->iobReserve.virtualAddress);
   OSDmemset(iob->iobReserve.virtualAddress,0,sizeof(SCSI_IOB_RESERVE));

   /* setup/validate temporary target task set handle */
   SCSI_SET_UNIT_HANDLE(&SCSI_ADPTSH(adapterTSH)->hhcb,
         &targetTSCB->unitControl,(SCSI_UEXACT8)targetTSCB->scsiID,
         (SCSI_UEXACT8)targetTSCB->lunID);

   /* setup iob for inquiry */
   iob->function = HIM_INITIATE_TASK;
   iob->taskSetHandle = (HIM_TASK_SET_HANDLE) targetTSCB;
   iob->flagsIob.autoSense = 1;
   iob->postRoutine = (HIM_POST_PTR) SCSIxPostSSU;
   iob->targetCommand = (void HIM_PTR) (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreUnlocked) + SCSI_MORE_INQCDB);
   iob->targetCommandLength = SCSI_SSU_CDB_SIZE;

   /* setup sense data */
   iob->errorData = (void HIM_PTR) (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreLocked) + SCSI_MORE_SNSDATA);
   iob->errorDataLength = SCSI_SIZE_SENSE_DATA;
   iob->taskAttribute = HIM_TASK_SIMPLE;

   /* Disable tagging */
   targetTSCB->targetAttributes.tagEnable = 0;
 
   /* setup cdb */
   uexact8Pointer = (SCSI_UEXACT8 HIM_PTR) iob->targetCommand;
   uexact8Pointer[0] = (SCSI_UEXACT8) 0x1B;
   uexact8Pointer[1] = (SCSI_UEXACT8) 0;
   uexact8Pointer[2] = (SCSI_UEXACT8) 0;
   uexact8Pointer[3] = (SCSI_UEXACT8) 0;
   uexact8Pointer[4] = (SCSI_UEXACT8) 0x01;
   uexact8Pointer[5] = (SCSI_UEXACT8) 0;

   /* execute it */
   SCSIQueueIOB(iob);
}

/*********************************************************************
*
*  SCSIxPostSSU
*
*     Verify SSU status
*
*  Return Value:  status
*                  
*  Parameters:    adapterTSH
*
*  Remarks:
*
*********************************************************************/
HIM_UINT32 SCSIxPostSSU (HIM_IOB HIM_PTR iob)
{
   SCSI_TARGET_TSCB HIM_PTR targetTSCB =
                              (SCSI_TARGET_TSCB HIM_PTR) iob->taskSetHandle;
   SCSI_UNIT_CONTROL HIM_PTR targetUnit = &targetTSCB->unitControl;
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = targetTSCB->adapterTSH;
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;

   /* check iob status to verify if device exist */
   switch(iob->taskStatus)
   {
      case HIM_IOB_GOOD:
      case HIM_IOB_DATA_OVERUNDERRUN:
         /* TUR was successful; Handle INQ */
         SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));
         SCSIxQueueTUR(adapterTSH);
         return(HIM_SUCCESS);

      case HIM_IOB_ABORTED_REQ_BUS_RESET:
         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_ABORTED_REQ_BUS_RESET;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
         return(HIM_SUCCESS);

      case HIM_IOB_ABORTED_CHIM_RESET:

         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_ABORTED_CHIM_RESET;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
         return(HIM_SUCCESS);
         
      case HIM_IOB_ABORTED_TRANSPORT_MODE_CHANGE:

         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_ABORTED_TRANSPORT_MODE_CHANGE;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
         return(HIM_SUCCESS);

      case HIM_IOB_ABORTED_3RD_PARTY_RESET:
         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_ABORTED_3RD_PARTY_RESET;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
         return(HIM_SUCCESS);

      case HIM_IOB_HOST_ADAPTER_FAILURE:
         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_HOST_ADAPTER_FAILURE;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
         return(HIM_SUCCESS);

      case HIM_IOB_BUSY:
      case HIM_IOB_TASK_SET_FULL:

         /* must free the current unit handle */
         SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));
      
         SCSIxQueueSpecialIOB(adapterTSH, HIM_UNFREEZE_QUEUE);
         
         /* if not HIM_PROBE, will do retry once */
         if (adapterTSH->retryBusScan <= 30000)
         { 
            /* must free the current unit handle */
            SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));
            ++adapterTSH->retryBusScan;
            SCSIxQueueSSU(adapterTSH);
            return(HIM_SUCCESS);
         }   
         break;

      default:
         /* treat device exist */
         /* must free the current unit handle */
         SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));
         SCSIxQueueBusScan(adapterTSH);
         return(HIM_SUCCESS);
   }
}

/*********************************************************************
*
*  SCSIxQueueTUR
*
*     Queue Test Unit Ready request. It is assumed that this routine
*     is called only during PAC (not HIM_PROBE). Also it is assumed
*     that this routine is called only for LUN 0.
*
*  Return Value:  none
*                  
*  Parameters:    adapterTSH
*
*  Remarks:
*
*********************************************************************/
void SCSIxQueueTUR (SCSI_ADAPTER_TSCB HIM_PTR adapterTSH)
{
   SCSI_TARGET_TSCB HIM_PTR targetTSCB = &adapterTSH->targetTSCBBusScan;
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   HIM_IOB HIM_PTR iob;
   HIM_UEXACT8 HIM_PTR uexact8Pointer;

   /* setup next iob request (inquiry) for bus scan */
   /* get iob memory */
   iob = (HIM_IOB HIM_PTR)(((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreUnlocked) + SCSI_MORE_IOB);
   OSDmemset(iob,0,sizeof(HIM_IOB));

   /* setup iob reserved memory */
   iob->iobReserve.virtualAddress = (void HIM_PTR) (((SCSI_UEXACT8 HIM_PTR)
               adapterTSH->moreLocked) + SCSI_MORE_IOBRSV);
   iob->iobReserve.bufferSize = sizeof(SCSI_IOB_RESERVE);
   iob->iobReserve.busAddress = OSMxGetBusAddress(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                  HIM_MC_LOCKED,iob->iobReserve.virtualAddress);
   OSDmemset(iob->iobReserve.virtualAddress,0,sizeof(SCSI_IOB_RESERVE));

   /* setup/validate temporary target task set handle */
   SCSI_SET_UNIT_HANDLE(&SCSI_ADPTSH(adapterTSH)->hhcb,
         &targetTSCB->unitControl,(SCSI_UEXACT8)targetTSCB->scsiID,
         (SCSI_UEXACT8)targetTSCB->lunID);

   /* setup iob for inquiry */
   iob->function = HIM_INITIATE_TASK;
   iob->taskSetHandle = (HIM_TASK_SET_HANDLE) targetTSCB;
   iob->flagsIob.autoSense = 1;
   iob->postRoutine = (HIM_POST_PTR) SCSIxPostTUR;
   iob->targetCommand = (void HIM_PTR) (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreUnlocked) + SCSI_MORE_INQCDB);
   iob->targetCommandLength = SCSI_TUR_CDB_SIZE;

   /* setup sense data */
   iob->errorData = (void HIM_PTR) (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreLocked) + SCSI_MORE_SNSDATA);
   iob->errorDataLength = SCSI_SIZE_SENSE_DATA;
   iob->taskAttribute = HIM_TASK_SIMPLE;

   /* Disable tagging */
   targetTSCB->targetAttributes.tagEnable = 0;
 
   /* setup cdb */
   uexact8Pointer = (SCSI_UEXACT8 HIM_PTR) iob->targetCommand;
   uexact8Pointer[0] = (SCSI_UEXACT8) 0;
   uexact8Pointer[1] = (SCSI_UEXACT8) 0;
   uexact8Pointer[2] = (SCSI_UEXACT8) 0;
   uexact8Pointer[3] = (SCSI_UEXACT8) 0;
   uexact8Pointer[4] = (SCSI_UEXACT8) 0;
   uexact8Pointer[5] = (SCSI_UEXACT8) 0;

   /* execute it */
   SCSIQueueIOB(iob);
}

/*********************************************************************
*
*  SCSIxPostTUR
*
*     Verify TUR status
*
*  Return Value:  status
*                  
*  Parameters:    iob
*
*  Remarks:
*
*********************************************************************/
HIM_UINT32 SCSIxPostTUR (HIM_IOB HIM_PTR iob)
{
   SCSI_TARGET_TSCB HIM_PTR targetTSCB =
                              (SCSI_TARGET_TSCB HIM_PTR) iob->taskSetHandle;
   SCSI_UNIT_CONTROL HIM_PTR targetUnit = &targetTSCB->unitControl;
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = targetTSCB->adapterTSH;
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;

   /* check iob status to verify if device exist */
   switch(iob->taskStatus)
   {
      case HIM_IOB_GOOD:
      case HIM_IOB_DATA_OVERUNDERRUN:
         /* TUR was successful; Handle INQ */
         SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));
         SCSIxQueueBusScan(adapterTSH);
         return(HIM_SUCCESS);

      case HIM_IOB_ABORTED_REQ_BUS_RESET:
         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_ABORTED_REQ_BUS_RESET;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
         return(HIM_SUCCESS);

      case HIM_IOB_ABORTED_CHIM_RESET:

         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_ABORTED_CHIM_RESET;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
         return(HIM_SUCCESS);
         
      case HIM_IOB_ABORTED_TRANSPORT_MODE_CHANGE:

         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_ABORTED_TRANSPORT_MODE_CHANGE;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
         return(HIM_SUCCESS);

      case HIM_IOB_ABORTED_3RD_PARTY_RESET:
         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_ABORTED_3RD_PARTY_RESET;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
         return(HIM_SUCCESS);

      case HIM_IOB_HOST_ADAPTER_FAILURE:
         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_HOST_ADAPTER_FAILURE;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
         return(HIM_SUCCESS);

      case HIM_IOB_BUSY:
      case HIM_IOB_TASK_SET_FULL:
         /* must free the current unit handle */
         SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));
      
         SCSIxQueueSpecialIOB(adapterTSH, HIM_UNFREEZE_QUEUE);
         
         /* if not HIM_PROBE, will do retry once */
         if (adapterTSH->retryBusScan <= 30000)
         { 
            /* must free the current unit handle */
            SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));
            ++adapterTSH->retryBusScan;
            SCSIxQueueTUR(adapterTSH);
            return(HIM_SUCCESS);
         }   
         break;

      default:
         /* treat device exist */
         /* must free the current unit handle */
         SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));
         SCSIxQueueSSU(adapterTSH);
         return(HIM_SUCCESS);
   }
}
#endif /* SCSI_PAC_SEND_SSU */

/*********************************************************************
*
*  SCSIxQueueBusScan
*
*     Queue request for bus scan operation
*
*  Return Value:  none
*                  
*  Parameters:    adapterTSH
*
*  Remarks:
*
*********************************************************************/
void SCSIxQueueBusScan (SCSI_ADAPTER_TSCB HIM_PTR adapterTSH)
{
   SCSI_TARGET_TSCB HIM_PTR targetTSCB = &adapterTSH->targetTSCBBusScan;
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   SCSI_DEVICE HIM_PTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetTSCB->scsiID];
   HIM_IOB HIM_PTR iob;
   HIM_BUS_ADDRESS busAddress;
#if !SCSI_DCH_U320_MODE
   HIM_UEXACT32 uexact32Value;
#else
   HIM_UEXACT32 uexact32Length;
#endif /* !SCSI_DCH_U320_MODE */
   HIM_UEXACT8 HIM_PTR uexact8Pointer;
   HIM_UINT8 probeFlag = (adapterTSH->iobProtocolAutoConfig->function == HIM_PROBE);
   HIM_BOOLEAN suppressNego = HIM_FALSE;
   HIM_TS_SCSI HIM_PTR transportSpecific;
   HIM_UEXACT8 currentSGListSize = sizeof(HIM_BUS_ADDRESS);
   HIM_UEXACT8 busAddressSize;

   busAddressSize = OSM_BUS_ADDRESS_SIZE;
#if ((OSM_BUS_ADDRESS_SIZE == 64) && SCSI_STANDARD_MODE)
   if (hhcb->SCSI_HF_SgBusAddress32 &&
       ((hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320) ||
        (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320)))
   {
      currentSGListSize = 4;
      busAddressSize = 32;
   }
#elif SCSI_DCH_U320_MODE
   /* DCH SCSI uses Generic SG List size */
   currentSGListSize = 8;
   busAddressSize = 32;

#endif /* ((OSM_BUS_ADDRESS_SIZE == 64) && SCSI_STANDARD_MODE) */

#if !SCSI_FAST_PAC
   transportSpecific =
      (HIM_TS_SCSI HIM_PTR)adapterTSH->iobProtocolAutoConfig->transportSpecific;
   
   if (transportSpecific)
   {
      /*
         Record the TargetID & Lun of current device being scanned.
         OSM can use this information to identify the device that failed during
         scsi bus scan which caused PAC completed with an error.
         This is not useful for SCSI_FAST_PAC implementation.
      */
      transportSpecific->scsiID = (HIM_UEXACT8) targetTSCB->scsiID;
      OSDmemset(transportSpecific->LUN,0,sizeof(transportSpecific->LUN));
      transportSpecific->LUN[1] = (HIM_UEXACT8) targetTSCB->lunID;
   }
#endif /* !SCSI_FAST_PAC */  

   /* setup next iob request (inquiry) for bus scan */
   /* get iob memory */
   iob = (HIM_IOB HIM_PTR)(((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreUnlocked) + SCSI_MORE_IOB);
   OSDmemset(iob,0,sizeof(HIM_IOB));

   /* setup iob reserved memory */
   iob->iobReserve.virtualAddress = (void HIM_PTR) (((SCSI_UEXACT8 HIM_PTR)
               adapterTSH->moreLocked) + SCSI_MORE_IOBRSV);
   iob->iobReserve.bufferSize = sizeof(SCSI_IOB_RESERVE);
   iob->iobReserve.busAddress = OSMxGetBusAddress(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                  HIM_MC_LOCKED,iob->iobReserve.virtualAddress);
   OSDmemset(iob->iobReserve.virtualAddress,0,sizeof(SCSI_IOB_RESERVE));

   /* setup/validate temporary target task set handle */
   SCSI_SET_UNIT_HANDLE(&SCSI_ADPTSH(adapterTSH)->hhcb,
         &targetTSCB->unitControl,(SCSI_UEXACT8)targetTSCB->scsiID,
         (SCSI_UEXACT8)targetTSCB->lunID);

   /* setup iob for inquiry */
   iob->function = HIM_INITIATE_TASK;
   iob->taskSetHandle = (HIM_TASK_SET_HANDLE) targetTSCB;
   iob->flagsIob.autoSense = 1;
   iob->postRoutine = (HIM_POST_PTR) SCSIxPostBusScan;
   iob->targetCommand = (void HIM_PTR) (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreUnlocked) + SCSI_MORE_INQCDB);
   iob->targetCommandLength = SCSI_INQ_CDB_SIZE;
#if (SCSI_NEGOTIATION_PER_IOB + SCSI_PARITY_PER_IOB + SCSI_SELTO_PER_IOB)
   if (probeFlag)
   {
      iob->transportSpecific =
         adapterTSH->iobProtocolAutoConfig->transportSpecific;
   }
#endif /* (SCSI_NEGOTIATION_PER_IOB + SCSI_PARITY_PER_IOB) */

   /* setup sense data */
   iob->errorData = (void HIM_PTR) (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreLocked) + SCSI_MORE_SNSDATA);
   iob->errorDataLength = SCSI_SIZE_SENSE_DATA;
   iob->taskAttribute = HIM_TASK_SIMPLE;

   if (targetTSCB->lunID == 0)
   { 
      targetTSCB->targetAttributes.tagEnable = 0;
   }
 
   /* setup cdb */
   uexact8Pointer = (SCSI_UEXACT8 HIM_PTR) iob->targetCommand;
   uexact8Pointer[0] = (SCSI_UEXACT8) 0x12;
   uexact8Pointer[1] = (SCSI_UEXACT8) 0;
   uexact8Pointer[2] = (SCSI_UEXACT8) 0;
   uexact8Pointer[3] = (SCSI_UEXACT8) 0;
   uexact8Pointer[5] = (SCSI_UEXACT8) 0;

   /* This is to avoid some SCSI 2 devices that didn't setup I_T_L nexus properly */
   /* through identify message. So LUN needs to be specified in inquiry cdb.      */ 
   if ((targetTSCB->lunID > 0) && (targetTSCB->lunID < 8) &&
       ((adapterTSH->scsi1OrScsi2Device & 
        ((SCSI_UEXACT16)(1 << targetTSCB->scsiID))) != 0))
   {
      uexact8Pointer[1] |= (SCSI_UEXACT8)(targetTSCB->lunID << 5);
   }

   /* expander checking only enable for LUN 0 */
   if ((hhcb->SCSI_HF_expSupport) && (targetTSCB->lunID == 0))
   {
      SCSI_ENABLE_EXP_STATUS(hhcb);
   }

   /* if HIM_PROBE, need to setup the OSM request data size if it is greater than default */ 
   if ((adapterTSH->iobProtocolAutoConfig->data.bufferSize > 
       SCSI_SIZE_INQUIRY) && probeFlag)
   {
      uexact8Pointer[4] = (SCSI_UEXACT8) adapterTSH->iobProtocolAutoConfig->data.bufferSize;
      busAddress = adapterTSH->iobProtocolAutoConfig->data.busAddress;
#if !SCSI_DCH_U320_MODE
      uexact32Value = (SCSI_UEXACT32) (0x80000000 + adapterTSH->iobProtocolAutoConfig->data.bufferSize);
#else     
      uexact32Length = (SCSI_UEXACT32) (adapterTSH->iobProtocolAutoConfig->data.bufferSize);
#endif /* (SCSI_DCH_U320_MODE) */
   }
   else
   {
      uexact8Pointer[4] = (SCSI_UEXACT8) SCSI_SIZE_INQUIRY;

      busAddress = OSMxGetBusAddress(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                     HIM_MC_LOCKED,(void HIM_PTR)
                     (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked) +
                     SCSI_MORE_INQDATA));

#if !SCSI_DCH_U320_MODE
      uexact32Value = (SCSI_UEXACT32)(0x80000000 + SCSI_SIZE_INQUIRY);
#else      
      uexact32Length = (SCSI_UEXACT32)(SCSI_SIZE_INQUIRY);
#endif /* (SCSI_DCH_U320_MODE) */
   }

   /* setup sg list */
   iob->data.virtualAddress = (void HIM_PTR)
      (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreLocked) + SCSI_MORE_INQSG);
   iob->data.bufferSize = 2 * currentSGListSize;
   iob->data.busAddress = OSMxGetBusAddress(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                              HIM_MC_LOCKED,iob->data.virtualAddress);

#if !SCSI_DCH_U320_MODE
   /* setup sg element */
#if ((OSM_BUS_ADDRESS_SIZE == 64) && SCSI_STANDARD_U320_MODE)
   SCSI_hPUTBUSADDRESSSG(hhcb,iob->data.virtualAddress,0,busAddress);
#else   
   SCSI_hPUTBUSADDRESS(hhcb,iob->data.virtualAddress,0,busAddress);
#endif /* ((OSM_BUS_ADDRESS_SIZE) == 64 && SCSI_STANDARD_U320_MODE) */

   HIM_PUT_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,iob->data.virtualAddress,
                           currentSGListSize,uexact32Value);

#else /* SCSI_DCH_U320_MODE */

   HIM_PUT_LITTLE_ENDIAN32(hhcb,
                          iob->data.virtualAddress,
                          0,\
                          (HIM_UEXACT32)(busAddress));
   /*  pad zeros for 64 bit SG address */
   HIM_PUT_LITTLE_ENDIAN32(hhcb,
                          iob->data.virtualAddress, 
                          4, \
                          0l);

   /* DCH uses common SG which supports uexact32 transfer length */
   HIM_PUT_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,iob->data.virtualAddress,
                           currentSGListSize,uexact32Length);
                           
   /* New for common SG List - set the 'LL' bit to indicate last list */ 
   /* Set ASEL to zero - memory allocation is internal for DCH core users */ 
   HIM_PUT_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,iob->data.virtualAddress,
                           currentSGListSize+4,0x40000000);
                           
#endif /* SCSI_DCH_U320_MODE) */

   HIM_FLUSH_CACHE(iob->data.virtualAddress,2*busAddressSize);

   if (probeFlag)
   {
      /* must be HIM_PROBE */
      /* obtain suppressNegotiation from transport specific */
      transportSpecific =
         (HIM_TS_SCSI HIM_PTR)adapterTSH->iobProtocolAutoConfig->transportSpecific;
      suppressNego = transportSpecific->suppressNego;
#if SCSI_DOMAIN_VALIDATION
      /* copy HIM_PROBE IOB's transportSpecific over to Inquiry IOB (Neptune update)*/
      iob->transportSpecific = (void HIM_PTR)
           (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreUnlocked) + SCSI_MORE_TRANSPORT_SPECIFIC);
      OSDmemcpy(iob->transportSpecific, transportSpecific, sizeof(HIM_TS_SCSI));
      
      /* make sure the HIM_PROBE's Inquiry IOB does not request DV */
      ((HIM_TS_SCSI HIM_PTR)(iob->transportSpecific))->dvIOB = HIM_FALSE;

#endif /* SCSI_DOMAIN_VALIDATION */

      if (targetTSCB->lunID != 0)
      {
         targetTSCB->targetAttributes.tagEnable = 
            (((deviceTable->SCSI_DF_disconnectEnable << targetTSCB->scsiID) &
               adapterTSH->tagEnable) != 0);

         /* pass HIM_PROBE IOB's transportSpecific over to Inquiry cmd IOB */
         /* if HIM_PORBE IOB was to non-zero LUN.                          */
         iob->transportSpecific = transportSpecific;
      }
   }

   /* If it is a HIM_PROBE or first LUN scanned in a PAC then fall into the */
   /* if statement */
   if (((targetTSCB->lunID == 0) && !probeFlag) || probeFlag)
   {
      /* Enter FORCE_NEGOTIATE path if the HIM_PROBE is for LUN 0 even the */
      /* 'suppressNego' flag is HIM_TRUE. */
      /* For OS like Netware, the driver will be unloaded/loaded many times  */
      /* or the server will be brought down and up.  These steps are invoked */
      /* without any change in scsi bus i.e. a wide/sync device will be      */
      /* running at wide/sync mode.  But, the driver will be running at      */
      /* narrow/async speed on the second or so load until the Domain        */
      /* Validation or a re-negotiation completed.  So, we must force        */
      /* negotiation for narrow/async mode inorder to avoid SCSI transfer    */
      /* rate mismatch.                                                      */
      if ((!probeFlag && (hhcb->SCSI_HF_resetSCSI))
          ||
          (probeFlag && 
           (((suppressNego == HIM_TRUE) && (targetTSCB->lunID != 0)) ||
            deviceTable->SCSI_DF_resetSCSI)))
      {
         /* suppress negotiation only after SCSI reset              */
         /* or if HIM_PROBE's suppressNego flag set to HIM_TRUE and */
         /* PROBE for non-zero Lun.                                 */
         SCSI_SUPPRESS_NEGOTIATION(&targetTSCB->unitControl);
      }
      else
      {
         /* Force negotiation for async/narrow if there is no bus reset. */
         /* This make an assumption that if a device is replaced,        */
         /* the new device should have the same characteristic           */
         /* as a replaced one e.g. wide, sync, dt, iu or qas support.    */

#if ((!SCSI_NEGOTIATION_PER_IOB) || SCSI_DOMAIN_VALIDATION)
         /* If force negotiation is succeeded, then we can setup    */
         /* transfer rate parameters to negotiate for async/narrow. */
         if (SCSI_FORCE_NEGOTIATION(&targetTSCB->unitControl) == SCSI_SUCCESS)
         {
            /* Disable DUALEDGE bit so that CHIM will always negotiate for   */
            /* async/narrow using WDTR and SDTR message but not PPR message. */
            SCSI_LAST_NEGO_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= ~SCSI_DUALEDGE;

            /* If the negoXferIndex points to SCSI_LAST_NEGO_XFER entry,    */
            /* this tells us the negotiation has been occurred at one time. */
            /* So, we must have a valid negotiated transfer paramters.      */
            /* Don't adjust the entry.  Otherwise, set it up to negotiate   */
            /* for narrow/async.                                            */
            if (deviceTable->negoXferIndex != SCSI_LAST_NEGO_ENTRY)
            {
               SCSI_LAST_NEGO_XFER(deviceTable)[SCSI_XFER_MISC_OPT] = 
                              SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_MISC_OPT];
               SCSI_LAST_NEGO_XFER(deviceTable)[SCSI_XFER_OFFSET] =
                              SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_OFFSET];
            }

            /* Force async/narrow */
            /* Use SCSI_TEMP_XFER entry for PAC/PROBE Inquiry cmd.      */
            /* Make SCSI_TEMP_XFER entry to be a current negoXferIndex. */
            deviceTable->negoXferIndex = SCSI_TEMP_ENTRY;
            SCSI_NEGO_XFER(deviceTable)[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
            SCSI_NEGO_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;
            SCSI_NEGO_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] = 0;
            SCSI_NEGO_XFER(deviceTable)[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;

#if SCSI_PACKETIZED_IO_SUPPORT
            /* When an U320 hard disk drive gets hot-swap, Unixware will rescan  */
            /* SCSI bus by sending down a PAC IOB.  During PAC, CHIM will set up */
            /* to renegotiate for async/narrow from Packetized mode on the first */
            /* inquiry command.  The negotiation fails.  Because initiator is    */
            /* still in packetized mode and sends out extended message to target */
            /* after selection with attention (this is how it is normally done   */
            /* for renegotiation in packetized mode).  In this case, initiator   */
            /* supposes to send out identify message follows by an extended      */
            /* message.  The target is in async/narrow mode and goes to bus free */
            /* when it received the extended message.                            */
            /* The fix is to clear the packetized bit in the current transfer    */
            /* entry.  This forces the renegotiation process to issue identify   */
            /* message before sending PPR message.                               */
            if (SCSI_hISPACKETIZED(deviceTable))
            {
               SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= ~SCSI_PACKETIZED;
            }

            /* Since we're going to negotiate for Narrow/Async, the switch to */
            /* packetized process does not need to be invoked.  The switching */
            /* process should be invoked only on the normal IOB.              */
            /* So, turn off switching to Packetized flag.                     */
            deviceTable->SCSI_DF_switchPkt = 0;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
         }
#endif /* ((!SCSI_NEGOTIATION_PER_IOB) || SCSI_DOMAIN_VALIDATION) */
      }
   }

   /* execute it */
   SCSIQueueIOB(iob);
}

/*********************************************************************
*
*  SCSIxPostBusScan
*
*     Verify bus scan operation
*
*  Return Value:  status
*                  
*  Parameters:    iob
*
*  Remarks:
*
*********************************************************************/
HIM_UINT32 SCSIxPostBusScan (HIM_IOB HIM_PTR iob)
{
   SCSI_TARGET_TSCB HIM_PTR targetTSCB =
                              (SCSI_TARGET_TSCB HIM_PTR) iob->taskSetHandle;
   SCSI_UNIT_CONTROL HIM_PTR targetUnit = &targetTSCB->unitControl;
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = targetTSCB->adapterTSH;
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   SCSI_DEVICE HIM_PTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetTSCB->scsiID];
   HIM_UEXACT8 uexact8Value;
   HIM_UEXACT8 maxTo8Lun = 0;
   HIM_UEXACT32 residual = 0;
   void HIM_PTR inquiryData;
   HIM_UEXACT32 inquiryDataSize;
   HIM_UEXACT8 peripheralDeviceType;
   HIM_UINT8 probeFlag = (adapterTSH->iobProtocolAutoConfig->function == HIM_PROBE);
#if SCSI_DOMAIN_VALIDATION
   HIM_UEXACT32 saveSize;
#if (SCSI_NEGOTIATION_PER_IOB)
   HIM_TS_SCSI HIM_PTR transportSpecific; 
#endif /* (SCSI_NEGOTIATION_PER_IOB)*/
#endif /* SCSI_DOMAIN_VALIDATION */

   /* Disable expander checking if it is LUN 0. It will be       */
   /* re-enabled when scanning the next ID LUN 0 in QueueBusScan */
   if ((hhcb->SCSI_HF_expSupport) && (targetTSCB->lunID == 0))
   {
      SCSI_DISABLE_EXP_STATUS(hhcb);
   }

   if (probeFlag)
   {
      /* Clear the device table's suppressNego flag.  The clear applies only  */
      /* for HIM_PROBE.  Since the suppressNego flag is set based on the value*/
      /* specified in Transport Specific of HIM_PROBE, clearing the flag will */
      /* assure the suppress negotiation is last only in the life of a        */
      /* HIM_PROBE IOB.                                                       */
      deviceTable->SCSI_DF_suppressNego = 0;
   }

   /* check iob status to verify if device exist */
   switch(iob->taskStatus)
   {
      case HIM_IOB_NO_RESPONSE:
         /* indicate device does not exist by setting the lunID to max. */
         /* LUN number so that CHIM continues scanning next scsi ID. */
         if (targetTSCB->lunID == 0)
         {
            targetTSCB->lunID = (SCSI_UINT8)adapterTSH->NumberLuns[targetTSCB->scsiID];
         }  
         break;

      case HIM_IOB_DATA_OVERUNDERRUN:
         residual = iob->residual;
            /* Fall through */

      case HIM_IOB_GOOD:
         if ((adapterTSH->iobProtocolAutoConfig->data.bufferSize > 
              SCSI_SIZE_INQUIRY) && probeFlag)
         {
            inquiryData = adapterTSH->iobProtocolAutoConfig->data.virtualAddress;
            inquiryDataSize = adapterTSH->iobProtocolAutoConfig->data.bufferSize;
         }
         else
         {
            inquiryData = ((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked) + SCSI_MORE_INQDATA;
            inquiryDataSize = SCSI_SIZE_INQUIRY;
         }
         /* for PAC if LUN is 0 or for HIM_PROBE probed then fall into the */
         /* if statement */
         if (((targetTSCB->lunID == 0) && !probeFlag) || probeFlag)
         {
#if SCSI_DOMAIN_VALIDATION
            if (hhcb->domainValidationMethod && (targetTSCB->lunID == 0))
            {
#if SCSI_DOMAIN_VALIDATION_ENHANCED
               /* If Inquiry Data was less than 57 bytes, */
               if ((inquiryDataSize - residual) < 57)
               {
                  /* then target does not support double-transition clocking. */
                  adapterTSH->SCSI_AF_dvLevel(targetTSCB->scsiID) = SCSI_DV_BASIC;
               }
               else  /* Inquiry Data return was greater 56 bytes */
               {
                  /* check CLOCKING field to determine level of Domain Validation */
                  HIM_GET_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                        &uexact8Value, inquiryData, 56);

                  switch ((uexact8Value >> 2) & 0x03)
                  {
                     case 0:  /* Support Single-Transition Clocking only */
                     case 2:  /* Reserved - we treat it like STC mode */
                        if (hhcb->domainValidationMethod > SCSI_DV_BASIC)
                        {
                           /* Can only perform basic DV */
                           adapterTSH->SCSI_AF_dvLevel(targetTSCB->scsiID) =
                                       SCSI_DV_BASIC;
                        }
                        else
                        {
                           /* Can perform any lower DV method */
                           adapterTSH->SCSI_AF_dvLevel(targetTSCB->scsiID) =
                                       hhcb->domainValidationMethod;
                        }
                        break;

                     case 1:  /* Support Dual-Transition Clocking only */
                     case 3:  /* Support BOTH DT & ST Clocking */
                        if (hhcb->domainValidationMethod >= SCSI_DV_ENHANCED)
                        {
                           /* Can perform enhanced DV */
                           adapterTSH->SCSI_AF_dvLevel(targetTSCB->scsiID) =
                                       SCSI_DV_ENHANCED;
                        }
                        else
                        {
                           /* Can perform any lower DV method */
                           adapterTSH->SCSI_AF_dvLevel(targetTSCB->scsiID) =
                                       hhcb->domainValidationMethod;
                        }
                        break;
                  }
               }
#else /* !SCSI_DOMAIN_VALIDATION_ENHANCED */
               adapterTSH->SCSI_AF_dvLevel(targetTSCB->scsiID) = SCSI_DV_BASIC;
#endif /* SCSI_DOMAIN_VALIDATION_ENHANCED */

               /* save the inquiry data for later reference */
               saveSize = inquiryDataSize - residual;
               /* the allocated size in moreUnlocked is SCSI_SIZE_INQUIRY  */
               /* cannot exceed it                                         */
               if (saveSize > SCSI_SIZE_INQUIRY)
               {
                  saveSize = SCSI_SIZE_INQUIRY;
               }
               OSDmemcpy(((SCSI_UEXACT8 HIM_PTR )adapterTSH->moreUnlocked) +
                     SCSI_MORE_REFDATA + targetTSCB->scsiID * SCSI_SIZE_INQUIRY,
                     inquiryData,
                     saveSize);
            }
#endif /* SCSI_DOMAIN_VALIDATION */

            /* invalidate cache */
            HIM_INVALIDATE_CACHE(inquiryData, inquiryDataSize);

            /* copy inquiry data to iob data buffer, this is to comply with */
            /* CHIM spec. v1.10 errata (2/2/99)                             */
            if ((adapterTSH->iobProtocolAutoConfig->data.bufferSize <= 
                 SCSI_SIZE_INQUIRY) && probeFlag)
            {
               OSDmemcpy(adapterTSH->iobProtocolAutoConfig->data.virtualAddress,
                         inquiryData,
                         adapterTSH->iobProtocolAutoConfig->data.bufferSize);
            }

            /* Is the Logical Unit supported? */
            HIM_GET_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                  &peripheralDeviceType, inquiryData, 0);

            if (probeFlag && (peripheralDeviceType == 0x7F))
            {
               targetTSCB->lunID = (SCSI_UINT8)adapterTSH->NumberLuns[targetTSCB->scsiID];
               /* This is an invalid LUN.                                        */
               /* The rest of inquiry data may not be reliable.                  */
               /* Break out from HIM_IOB_DATA_OVERUNDERRUN & HIM_IOB_GOOD case.  */
               break;
            }
            else
            {
               /* set device exist */
               SCSIxSetLunExist(adapterTSH->lunExist,targetTSCB->scsiID,targetTSCB->lunID);

               if (!probeFlag)
               {
                  /* Clear resetSCSI flag during the regular PAC so that when */
                  /* the subsequent HIM_PROBE invoked for the same id,        */
                  /* suppress negotiation will not get called. */
                  deviceTable->SCSI_DF_resetSCSI = 0;
               }
            }

            /* adjust transfer option based on inquiry data */
            HIM_GET_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,&uexact8Value,
                  inquiryData, 7);

            /* Since the Inquiry data is valid, we can make TARGET_CAP_XFER */
            /* entry to be a current negoXferIndex.                         */
            deviceTable->negoXferIndex = SCSI_TARGET_CAP_ENTRY;

            /* Update wide support only if the target is not a wide device */
            /* and previously it was not running in wide mode. */
            if ((!(uexact8Value & 0x20)) && 
               (!(SCSI_LAST_NEGO_XFER(deviceTable)[SCSI_XFER_MISC_OPT] & SCSI_WIDE)))
            {
               /* non wide */
               SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
            }
            else
            {
               /* wide */
               SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_MISC_OPT] |= SCSI_WIDE;
            }

            /* Update sync support only if the target is not a sync. device */
            /* and previously it was not running in sync. mode. */
            /* Compatibility issue, a target reports as async. device in */
            /* inquiry data but it initiates sync. nego. to HA. */
            if ((!(uexact8Value & 0x10)) &&
                (!SCSI_LAST_NEGO_XFER(deviceTable)[SCSI_XFER_OFFSET]))
            {
               /* non sync */
               SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;
            }
            else
            {
               /* @REVISIT - we might set to 1 temporary and update          */
               /*            the actual offset value after negotiation done. */
               /* sync */
               SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_OFFSET] = SCSI_MAX_OFFSET;

               /* @REVISIT - we might set to other value temporary and update */
               /*            the actual period value after negotiation done.  */
               SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PERIOD] = 0x0A;
            }

#if SCSI_DOMAIN_VALIDATION
            if (hhcb->domainValidationMethod && (targetTSCB->lunID == 0))
            {
               if (!SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_OFFSET] &&
                   !(SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_MISC_OPT] & SCSI_WIDE))
               {
                  adapterTSH->SCSI_AF_dvLevel(targetTSCB->scsiID) = SCSI_DV_DISABLE;
               }

               /* Check if DV ID mask indicates DV is not allowed on  */
               /* this device for PAC.                                */
               if ((!probeFlag) &&
                   (!(adapterTSH->domainValidationIDMask &
                      (1 << targetTSCB->scsiID))))
               {
                  adapterTSH->SCSI_AF_dvLevel(targetTSCB->scsiID) = SCSI_DV_DISABLE;
               }
            }
#endif /* SCSI_DOMAIN_VALIDATION */

            /* adjust scontrol based on tag enable */
            if (uexact8Value & 0x02)
            {
               adapterTSH->tagEnable |= (1 << targetTSCB->scsiID);
            }

#if SCSI_PPR_ENABLE
            /* If Inquiry Data was less than 57 bytes, */
            if ((inquiryDataSize - residual) < 57)
            {
               /* then target does not support double-transition clocking, */
               /* quick arbitrate, and packetized transfer.                */
               SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &=
                           ~(SCSI_QUICKARB | SCSI_DUALEDGE | SCSI_PACKETIZED);
            }
            else  /* Inquiry Data return was greater 57 bytes */
            {
               /* Udpate Single-Transition clocking (STC), Dual-Transition */
               /* clocking (DTC), Quick Arbitrate supported (QAS), and     */
               /* Information Unit/Packetized support (IUS) based on the   */
               /* Inquiry data info stored at byte 56.                     */
               HIM_GET_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                     &uexact8Value, inquiryData, 56);

               /* Apply STC & DTC */
               switch ((uexact8Value >> 2) & 0x03)
               {
                  case 2:  /* reserved - we assume STC mode */
                  case 0:
                     /* Support single-transition clocking ONLY */
                     SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= ~SCSI_DUALEDGE;
                     /* @REVISIT deviceTable->SCSI_DF_stcSupport = 1;*/
                     break;

                  case 1:
                     /* Support double-transition clocking ONLY */
                     SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] |= SCSI_DUALEDGE;
                     /* @REVISIT deviceTable->SCSI_DF_stcSupport = 0;*/
                     break;
                  
                  case 3:
                     /* Support BOTH double & single transition clocking */
                     SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] |= SCSI_DUALEDGE;
                     /* @REVISIT deviceTable->SCSI_DF_stcSupport = 1;*/
                     break;
               }

               /* Apply TARGET_CAP's IUS and QAS based on the DUALEDGE bit */
               if (SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] & SCSI_DUALEDGE)
               {
                  /* Update transfer period to fastest DT non-IU rate. */ 
                  SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PERIOD] = 0x09;

#if SCSI_PACKETIZED_IO_SUPPORT
                  /* Apply IUS */
                  if (uexact8Value & 0x01)
                  {
                     SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] |=
                            SCSI_PCOMP_EN | SCSI_RTI | SCSI_WR_FLOW | 
                              SCSI_RD_STRM | SCSI_HOLD_MCS | SCSI_PACKETIZED;

                     /* Update transfer period to fastest IU rate. */ 
                     SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PERIOD] = 0x08;
                  }
                  else
                  {
                     SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &=
                       ~(SCSI_PCOMP_EN | SCSI_WR_FLOW | SCSI_RD_STRM | 
                               SCSI_HOLD_MCS | SCSI_RTI | SCSI_PACKETIZED);
                  }
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

                  /* Apply QAS */
                  if (uexact8Value & 0x02)
                  {
                     SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] |=
                                                               SCSI_QUICKARB;
                  }
                  else
                  {
                     SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &=
                                                               ~SCSI_QUICKARB;
                  }
               }
               else
               {
                  /* Adjust TARGET_CAP's transfer period if DT not support.   */
                  /* Assume target can run at Ultra-2 speed (80MB/s).         */
                  /* @REVISIT - we might set to other value temporary and     */
                  /*   update the actual period value after negotiation done. */
                  SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PERIOD] = 0x0A;
                  
                  /* Clear IUS and QAS */
                  SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &=
                                             ~(SCSI_QUICKARB | SCSI_PACKETIZED);
               }
            }
#if SCSI_OEM1_SUPPORT
            /*If this device not IU or QAS then no QAS for anyone if OEM1*/
            if ( (SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &
                   (SCSI_QUICKARB | SCSI_PACKETIZED)) !=
                        (SCSI_QUICKARB | SCSI_PACKETIZED))
            {       
               if (hhcb->OEMHardware == SCSI_OEMHARDWARE_OEM1)
               {
                  hhcb->SCSI_HF_adapterQASDisable =1;
               }   
            }   
#endif /* SCSI_OEM1_SUPPORT */
#endif /* SCSI_PPR_ENABLE */
         }
         else /* else PAC's Id not 0 or not probed */
         {
            /* invalidate cache */
            HIM_INVALIDATE_CACHE(inquiryData, inquiryDataSize);

            /* adjust transfer option based on inquiry data */
            HIM_GET_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,&peripheralDeviceType,
                inquiryData, 0);

            /* For PAC, CHIM will scan up to the maximum LUN that a    */
            /* target can support.                                     */
            /* If the Logical Unit not support, advance to next target */
            if ((peripheralDeviceType != 0x7F) && (peripheralDeviceType != 0x20))
            {
               /* set device exist */
               SCSIxSetLunExist(adapterTSH->lunExist,targetTSCB->scsiID,targetTSCB->lunID);
            }
         }  /* PAC's Id0 or probed */

         /* obtain ANSI-approved version: SCSI-1, SCSI-2 or so on */
         HIM_GET_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,&uexact8Value,
             inquiryData, 2);

         /* adjust max. lun support up to 8 for SCSI 1 or 2 device */
         if ((uexact8Value & 0x07) < 3)
         {
            maxTo8Lun = 1;
         
            if (targetTSCB->lunID == 0)
            {
               /* indicate pre SCSI-3 device */ 
               adapterTSH->scsi1OrScsi2Device |=
                 (SCSI_UEXACT16) (1 << targetTSCB->scsiID);
            }

            /* to turn-off tag queueing for SCSI-1 device */
            if (uexact8Value & 0x01)
            {
               adapterTSH->tagEnable &= ~(1 << targetTSCB->scsiID);
            }
         }
         else
         {
            if (targetTSCB->lunID == 0)
            {
               /* indicate SCSI-3 device */ 
               adapterTSH->scsi1OrScsi2Device &=
                  (SCSI_UEXACT16) (~(1 << targetTSCB->scsiID));
            }
         }                                 

#if SCSI_PAC_SEND_SSU
#if SCSI_FAST_PAC 
         HIM_GET_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
               &uexact8Value, inquiryData, 0);
         if (((uexact8Value & 0x1F) == 0) &&
               !(adapterTSH->SCSI_AF_ssuInProgress))
         {
            adapterTSH->SCSI_AF_ssuInProgress = 1;
            /* Inquiry was successful on a disk device; Handle TUR/SSU */
            SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));
            SCSIxQueueTUR(adapterTSH);
            return(HIM_SUCCESS);
         }
#else

         /* Attempt to spin up disk drive type device during PAC */
         if ((targetTSCB->lunID == 0) && !probeFlag)
         {
            HIM_GET_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                  &uexact8Value, inquiryData, 0);
            if (((uexact8Value & 0x1F) == 0) &&
                  !(adapterTSH->SCSI_AF_ssuInProgress))
            {
               adapterTSH->SCSI_AF_ssuInProgress = 1;
               /* Inquiry was successful on a disk device; Handle TUR/SSU */
               SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));
               SCSIxQueueTUR(adapterTSH);
               return(HIM_SUCCESS);
            }
         }
#endif /* SCSI_FAST_PAC */
#endif /* SCSI_PAC_SEND_SSU */

         /* Support certain boards that do not allow the hard disk drive */
         /* to be disconnected on any SCSI command. */
         if (hhcb->SCSI_HP_disconnectOffForHDD)
         {
            /* Obtain Peripheral device qualifier and type */
            HIM_GET_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,&uexact8Value,
                inquiryData, 0);

            /* If direct-access device type (ie. hard disk) disable disconnect */
            if ((uexact8Value & 0x1F) == 0)
            {
               /* Obtain the second byte to get RMB bit */
               HIM_GET_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                   &uexact8Value, inquiryData, 1);

               if (!(uexact8Value & 0x80))
               {
                  SCSI_DEVICE_TABLE(hhcb)[targetTSCB->scsiID].SCSI_DF_disconnectEnable = 0;
               }
            }
         }

         break;

      case HIM_IOB_ABORTED_REQ_BUS_RESET:
         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_ABORTED_REQ_BUS_RESET;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
#if SCSI_PAC_SEND_SSU
         adapterTSH->SCSI_AF_ssuInProgress = 0;
#endif /* SCSI_PAC_SEND_SSU */
         return(HIM_SUCCESS);

      case HIM_IOB_ABORTED_CHIM_RESET:

         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_ABORTED_CHIM_RESET;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
#if SCSI_PAC_SEND_SSU
         adapterTSH->SCSI_AF_ssuInProgress = 0;
#endif /* SCSI_PAC_SEND_SSU */
         return(HIM_SUCCESS);
         
      case HIM_IOB_ABORTED_TRANSPORT_MODE_CHANGE:

         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_ABORTED_TRANSPORT_MODE_CHANGE;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
#if SCSI_PAC_SEND_SSU
         adapterTSH->SCSI_AF_ssuInProgress = 0;
#endif /* SCSI_PAC_SEND_SSU */
         return(HIM_SUCCESS);

      case HIM_IOB_ABORTED_3RD_PARTY_RESET:
         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_ABORTED_3RD_PARTY_RESET;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
#if SCSI_PAC_SEND_SSU
         adapterTSH->SCSI_AF_ssuInProgress = 0;
#endif /* SCSI_PAC_SEND_SSU */
         return(HIM_SUCCESS);

      case HIM_IOB_HOST_ADAPTER_FAILURE:
         /* abort protocol auto config because of a catastrophic event */
         /* post protocol auto config back to osm */
         adapterTSH->iobProtocolAutoConfig->taskStatus = HIM_IOB_HOST_ADAPTER_FAILURE;
         adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
         return(HIM_SUCCESS);

      case HIM_IOB_BUSY:
      case HIM_IOB_TASK_SET_FULL:
         /* must free the current unit handle */
         SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));
      
         SCSIxQueueSpecialIOB(adapterTSH, HIM_UNFREEZE_QUEUE);
         
         /* if not HIM_PROBE, will do retry once */
         if ((adapterTSH->iobProtocolAutoConfig->function != HIM_PROBE) && 
             (adapterTSH->retryBusScan <= 30000))
         { 
            /* must free the current unit handle */
            SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));
            ++adapterTSH->retryBusScan;
            SCSIxQueueBusScan(adapterTSH);
#if SCSI_PAC_SEND_SSU
            adapterTSH->SCSI_AF_ssuInProgress = 0;
#endif /* SCSI_PAC_SEND_SSU */
            return(HIM_SUCCESS);
         }   
         break;
        

      default:
         /* treat device exist */
         if (!adapterTSH->retryBusScan)
         {
            /* must free the current unit handle */
            SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));

            ++adapterTSH->retryBusScan;
            SCSIxQueueBusScan(adapterTSH);
#if SCSI_PAC_SEND_SSU
            adapterTSH->SCSI_AF_ssuInProgress = 0;
#endif /* SCSI_PAC_SEND_SSU */
            return(HIM_SUCCESS);
         }
         break;
   } /* switch taskStatus */

   /* free the current unit handle */
   SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));

   if (!probeFlag)
   {
      /* work on next lun */
      ++targetTSCB->lunID;
      
      if ((!(hhcb->multipleLun & (1 << (SCSI_UEXACT16)targetTSCB->scsiID))) ||
          (maxTo8Lun && (targetTSCB->lunID > 7)) ||
          (targetTSCB->lunID >= (SCSI_UINT8)adapterTSH->NumberLuns[targetTSCB->scsiID]))
      {
#if SCSI_DOMAIN_VALIDATION
         if (hhcb->domainValidationMethod &&
             (adapterTSH->SCSI_AF_dvLevel(targetTSCB->scsiID) !=
              SCSI_DV_DISABLE))
         {
            /* restore default transfer parameters before exercising */
            /* domain validation */
            SCSIxRestoreDefaultXfer(adapterTSH, targetTSCB);
         }
         else
#endif /* SCSI_DOMAIN_VALIDATION */

#if (!SCSI_NEGOTIATION_PER_IOB)
         {
            /* CHIM forces negotiation for Narrow/Async. on every first PAC's */
            /* Inquiry cmd except if resetSCSI flag is not set and since      */
            /* Domain Validation is not going to perform, CHIM needs to force */
            /* negotiation on the next IO to bring target back to full speed. */
            SCSI_FORCE_NEGOTIATION(&targetTSCB->unitControl);

#if SCSI_PACKETIZED_IO_SUPPORT
            /* Turn on the 'switch to packetized' flag so that the first */
            /* tagged command IOB after the PAC will invoke the switch   */
            /* to packetized process.  And if both initiator/target      */
            /* support and configure for packetized mode, then both will */
            /* be running in packetized mode.                            */
            deviceTable->SCSI_DF_switchPkt = 1;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
         }
#else  /* (!SCSI_NEGOTIATION_PER_IOB) */

#if SCSI_DOMAIN_VALIDATION
         if ((transportSpecific =
              (HIM_TS_SCSI HIM_PTR)adapterTSH->iobProtocolAutoConfig->transportSpecific))
         {
            if (transportSpecific->forceSync && transportSpecific->forceWide)
            {
               if (hhcb->SCSI_HF_resetSCSI)
               {
                  /* restores negotiation if it's needed for the current target */
                  SCSI_FORCE_NEGOTIATION(&targetTSCB->unitControl);
               }
            }
         }
#endif /* SCSI_DOMAIN_VALIDATION */

#endif /* (!SCSI_NEGOTIATION_PER_IOB) */

         /* next lun not available and need to work on next target instead */
         for (;++targetTSCB->scsiID < adapterTSH->hhcb.maxDevices;)
         {
            if (adapterTSH->NumberLuns[targetTSCB->scsiID] == 0)
            {
               /* If NumberLuns specified for the target is 0, skip this target */            
               continue;
            }   
            else
            {
               targetTSCB->lunID = 0;
               if (targetTSCB->scsiID == adapterTSH->hhcb.hostScsiID)
               {
#if !SCSI_NULL_SELECTION
                  /* skip the host scsi id */
                  continue;
#else
                  /* Don't skip host scsi id - attempt Null Selection */
                  /* Can't disconnect on Null Selection */
                  /* targetTSCB->scsiID has been changed, we must use */
                  /* SCSI_DEVICE_TABLE macro to reference its field.  */
                  SCSI_DEVICE_TABLE(hhcb)[targetTSCB->scsiID].SCSI_DF_disconnectEnable = 0;
                  break;
#endif /* !SCSI_NULL_SELECTION */
               }
               break;
            }
         }
      }

      if (targetTSCB->scsiID >= adapterTSH->hhcb.maxDevices)
      {
         /* Clear resetSCSI flag after the bus scan is done so that */
         /* the subsequence PROTO_AUTO_CONFIG invokes, the suppress */
         /* negotiation will not get call. */
         hhcb->SCSI_HF_resetSCSI = 0;

         /* Store PAC IOB status in temporary storage first.               */
         /* Per CHIM spec, 'iobProtocolAutoConfig->taskStatus' should only */
         /* be updated after PAC is fully completed.                       */
         
         adapterTSH->pacIobStatus = HIM_IOB_GOOD;

#if SCSI_DOMAIN_VALIDATION
         if (hhcb->domainValidationMethod)
         {
            SCSIxSetupDomainValidation(adapterTSH);
            SCSIxQueueDomainValidation(adapterTSH);
         }
         else
#endif /* SCSI_DOMAIN_VALIDATION */
         {
            adapterTSH->iobProtocolAutoConfig->taskStatus = adapterTSH->pacIobStatus;
            adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
            /* If OSM is frozen then unfreeze the OSM because the OSMEvent handling */
            /* is complete */
            if (adapterTSH->SCSI_AF_osmFrozen)
            {
               /* Set OSM is Unfrozen flag */
               adapterTSH->SCSI_AF_osmFrozen = 0;
               /* Call OSM with Unfreeze Event. */
               OSMxEvent(adapterTSH->osmAdapterContext,HIM_EVENT_OSMUNFREEZE,(void HIM_PTR) 0);
            }
         }
      }
      else
      {
         /* continue with bus scan process */
         adapterTSH->retryBusScan = 0;
         SCSIxQueueBusScan(adapterTSH);
      }
   }
#if !SCSI_DISABLE_PROBE_SUPPORT
   else /* Process HIM_PROBE status and DV if enable and probe function*/
   {
         SCSIxPostProbe(iob,residual,peripheralDeviceType);
   }
#endif /* !SCSI_DISABLE_PROBE_SUPPORT */

#if SCSI_PAC_SEND_SSU
   adapterTSH->SCSI_AF_ssuInProgress = 0;
#endif /* SCSI_PAC_SEND_SSU */

   return(HIM_SUCCESS);
}

/*********************************************************************
*
*  SCSIxPostProbe
*
*     Verify probe
*
*  Return Value:  none
*                  
*  Parameters:    iob
*                 residual
*                 peripheralDeviceType
*
*  Remarks:       PostBusScan function call for HIM_PROBE function
*
*********************************************************************/
#if !SCSI_DISABLE_PROBE_SUPPORT
void SCSIxPostProbe (HIM_IOB HIM_PTR iob,
                     HIM_UEXACT32 residual,
                     HIM_UEXACT8 peripheralDeviceType)
{
   SCSI_TARGET_TSCB HIM_PTR targetTSCB =
                              (SCSI_TARGET_TSCB HIM_PTR) iob->taskSetHandle;
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = targetTSCB->adapterTSH;
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   SCSI_DEVICE HIM_PTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetTSCB->scsiID];
   HIM_UEXACT8 HIM_PTR lunExist = SCSI_ADPTSH(adapterTSH)->lunExist;
   HIM_UEXACT8 HIM_PTR tshExist = SCSI_ADPTSH(adapterTSH)->tshExist;
   HIM_UINT8 i;
   HIM_UINT8 j;
   HIM_TS_SCSI HIM_PTR transportSpecific;
#if SCSI_DOMAIN_VALIDATION
   HIM_UEXACT8 doDV = 0;
#endif /* SCSI_DOMAIN_VALIDATION */

#if SCSI_DOMAIN_VALIDATION
   /* We should do domain validation only for LUN 0 and not SCSI_DV_DISABLE */
   if ((targetTSCB->lunID == 0) && (hhcb->domainValidationMethod))
   {
      transportSpecific =
         (HIM_TS_SCSI HIM_PTR)adapterTSH->iobProtocolAutoConfig->transportSpecific;
      if (transportSpecific->dvIOB == HIM_TRUE)
      {
         doDV = 1;
      }
   }

   /* restore default xfer speed and width before exercising */
   /* domain validation */
   if (doDV)
   {
      SCSIxRestoreDefaultXfer(adapterTSH, targetTSCB);
   }
   else
#endif /* SCSI_DOMAIN_VALIDATION */
   {
      transportSpecific =
         (HIM_TS_SCSI HIM_PTR)adapterTSH->iobProtocolAutoConfig->transportSpecific;

#if (!SCSI_NEGOTIATION_PER_IOB)
      /* CHIM forces negotiation for Narrow/Async. on every first HIM_PROBE */
      /* Inquiry cmd if resetSCSI flag is not set, suppressNego flag is     */
      /* HIM_FALSE and since the Domain Validation is not going to perform, */
      /* CHIM needs to force negotiation on the next IO to bring the target */
      /* back to full speed.                                                */
      /* Make TARGET_CAP_XFER entry to be current negoXferIndex since the   */
      /* last Narrow/Async negotiation has changed the negoXferIndex point  */
      /* to LAST_NEGO_XFER entry and it is currently in Narrow/Async mode.  */
      /* Beside, the negoXferIndex will not get update in SCSIxPostBusScan  */
      /* to point to TARGET_CAP_XFER entry because the last scanned LUN     */
      /* might be invalid LUN.  If we do not set the negoXferIndex here     */
      /* then it is impossible for the force negotiation to really happen.  */
      deviceTable->negoXferIndex = SCSI_TARGET_CAP_ENTRY;
      SCSI_FORCE_NEGOTIATION(&targetTSCB->unitControl);

#if SCSI_PACKETIZED_IO_SUPPORT
      /* Turn on the 'switch to packetized' flag so that the first tagged */
      /* command IOB after the PAC will invoke the switch to packetized   */
      /* process.  And if both initiator/target support and configure for */
      /* packetized mode, then both will be running in packetized mode.   */
      deviceTable->SCSI_DF_switchPkt = 1;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

#else /* SCSI_NEGOTIATION_PER_IOB */

#if SCSI_DOMAIN_VALIDATION
      if (transportSpecific)
      {
         if (transportSpecific->forceSync && transportSpecific->forceWide)
         {
            if (deviceTable->SCSI_DF_resetSCSI &&
                (transportSpecific->suppressNego == HIM_FALSE))
            {
               /* restores negotiation if it's needed for the current target */
               SCSI_FORCE_NEGOTIATION(&targetTSCB->unitControl);
            }
         }
      }
#endif /* SCSI_DOMAIN_VALIDATION */

#endif /* (!SCSI_NEGOTIATION_PER_IOB) */
   }  /* doDV else */

   /* post HIM_PROBE IOB back to osm */
   if ((iob->taskStatus == HIM_IOB_GOOD) ||
       (iob->taskStatus == HIM_IOB_DATA_OVERUNDERRUN))
   {
      /* Clear resetSCSI flag after the HIM_PROBE is done so that when */
      /* the subsequent HIM_PROBE invoked for the same id, the suppress */
      /* negotiation will not get called. */
      deviceTable->SCSI_DF_resetSCSI = 0;

      i = SCSI_ADPTSH(adapterTSH)->lastScsiIDProbed;
      j = SCSI_ADPTSH(adapterTSH)->lastScsiLUNProbed;
      if (SCSIxChkLunExist(lunExist,i,j) != SCSIxChkLunExist(tshExist,i,j))
      {
         /* if there are non-zero HIM_PORBE's bufferSize and residual, */
         /* then the taskStatus = HIM_IOB_DATA_OVERUNDERRUN.           */
         if (adapterTSH->iobProtocolAutoConfig->data.bufferSize && residual)
         {
            adapterTSH->pacIobStatus = HIM_IOB_DATA_OVERUNDERRUN;

            /* we only need to report residual if the actual transfer   */
            /* size is smaller than HIM_PROBE's bufferSize.             */
            /* this is to comply with CHIM spec. v1.10 errata (2/2/99)  */

            /* Is HIM_PROBE's bufferSize greater than the default size? */
            if (adapterTSH->iobProtocolAutoConfig->data.bufferSize >
                SCSI_SIZE_INQUIRY)
            {
               /* residual value is for the HIM_PROBE's bufferSize */
               adapterTSH->iobProtocolAutoConfig->residual = residual;
            }

            /* HIM_PROBE's bufferSize is equal or less than the default size */
            else
            {
               /* if the actual data xfer is less than HIM_PORBE's bufferSize */
               if (adapterTSH->iobProtocolAutoConfig->data.bufferSize >
                   (SCSI_SIZE_INQUIRY - residual))
               {
                  /* then we do have residual to return */
                  adapterTSH->iobProtocolAutoConfig->residual = 
                     adapterTSH->iobProtocolAutoConfig->data.bufferSize -
                     (SCSI_SIZE_INQUIRY - residual);
               }
               else
               {
                  /* No residual needs to return and the taskStatus should be */
                  /* changed to HIM_IOB_GOOD. */
                  adapterTSH->pacIobStatus = HIM_IOB_GOOD;
               }
            }
         }
         else
         {
            adapterTSH->pacIobStatus = HIM_IOB_GOOD;
         }
      }
      else
      {
         if (peripheralDeviceType == 0x7F)
         {
            adapterTSH->pacIobStatus = HIM_IOB_INVALID_LUN;
         }
         else
         {
            /* This is still an question whether we might go here??? */
            adapterTSH->pacIobStatus = HIM_IOB_NO_RESPONSE;
         }
      }
   }
   else
   {
      adapterTSH->pacIobStatus = iob->taskStatus;
   }
   
#if SCSI_DOMAIN_VALIDATION
   if (doDV)
   {
      SCSIxSetupDomainValidation(adapterTSH);
      SCSIxQueueDomainValidation(adapterTSH);
   }
   else
#endif /* SCSI_DOMAIN_VALIDATION */
   {
      adapterTSH->iobProtocolAutoConfig->taskStatus = adapterTSH->pacIobStatus;      
      adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
   }
}
#endif /* !SCSI_DISABLE_PROBE_SUPPORT */

/*********************************************************************
*
*  SCSIxRestoreDefaultXfer
*
*     Restore default xfer speed and width to prepare for
*     domain validation
*
*  Return Value:  none
*                  
*  Parameters:    adapterTSH
*                 targetTSCB
*
*  Remarks:
*
*********************************************************************/
#if SCSI_DOMAIN_VALIDATION
void SCSIxRestoreDefaultXfer (SCSI_ADAPTER_TSCB HIM_PTR adapterTSH,
                              SCSI_TARGET_TSCB HIM_PTR targetTSCB)
{
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   SCSI_DEVICE HIM_PTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetTSCB->scsiID];
   SCSI_UEXACT8 i;

   if (!(adapterTSH->SCSI_AF_dvThrottleOSMReset(targetTSCB->scsiID)))
   {
      /* restore default xfer speed and width before exercising */
      /* domain validation */
      for (i = 0; i < SCSI_MAX_XFER_PARAMETERS; i++)
      {
         SCSI_DV_XFER(deviceTable)[i] = SCSI_TARGET_CAP_XFER(deviceTable)[i];
      }

      /* The default speed will be the lower one between the target */
      /* capability entry and the current default transfer entry.   */

      /* Restore the defaultXferIndex to preDVXferIndex.      */
      /* Because the defaultXferIndex might point to DV entry */
      /* as the result of previous DV process.                */
      deviceTable->defaultXferIndex = deviceTable->preDVXferIndex;

      /* Make sure we don't go over the default xfer rate limit. */
      /* Take the lower offset. */
      if (SCSI_DV_XFER(deviceTable)[SCSI_XFER_OFFSET] > 
          SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_OFFSET])
      {
         SCSI_DV_XFER(deviceTable)[SCSI_XFER_OFFSET] =
                        SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_OFFSET];
      }

      /* Take the higher transfer period. */
      /* The higher transfer period means slower transfer rate. */
      if (SCSI_DV_XFER(deviceTable)[SCSI_XFER_PERIOD] <
          SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_PERIOD])
      {
         SCSI_DV_XFER(deviceTable)[SCSI_XFER_PERIOD] =
                        SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_PERIOD];
      }

      /* Clear the PCOMP, RD_STRM, WR_FLOW, QAS, DT or IU bit if not enable. */
      SCSI_DV_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &=
                        SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT];
#if SCSI_OEM1_SUPPORT
      /* A temporary solution to resolve the issue with the OEM1 U160 JBOD */
      /* box that might not able to handle packetized and QAS.              */
      /* Therefore, if it is the OEM1 hardware and the transfer period is   */
      /* U160 speed (0x09), CHIM will not negotiate to run at U160 plus     */
      /* Packetized, QAS and data streaming.  But, CHIM will negotiate to   */
      /* run only at U160 speed.                                            */
      if ((hhcb->OEMHardware == SCSI_OEMHARDWARE_OEM1) &&
          (SCSI_DV_XFER(deviceTable)[SCSI_XFER_PERIOD] == 0x09))
      {
         SCSI_DV_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= 
                  ~(SCSI_RD_STRM | SCSI_WR_FLOW | SCSI_QAS_REQ | SCSI_IU_REQ);
      }
#endif /* SCSI_OEM1_SUPPORT */

      /* Clear the Wide bit if not enable. */
      SCSI_DV_XFER(deviceTable)[SCSI_XFER_MISC_OPT] &=
                        SCSI_DEFAULT_XFER(deviceTable)[SCSI_XFER_MISC_OPT];

      /* Make DV entry to be the default transfer index, defaultXferIndex.    */
      /* The reason is to make sure CHIM will maintain the successfull        */
      /* negotiated speed of Domain Validation until another DV occurs or     */
      /* OSM changes the default transfer rate parameters via targer profile. */
      deviceTable->defaultXferIndex = SCSI_DV_ENTRY;
   }
   else
   {
      adapterTSH->SCSI_AF_dvThrottleOSMReset(targetTSCB->scsiID) = 0;
   }
}
#endif /* SCSI_DOMAIN_VALIDATION */

/*********************************************************************
*
*  SCSIxDvForceNegotiation
*
*     Force Negotiation with the given xfer parameters for
*     domain validation    
*
*  Return Value:  none
*                  
*  Parameters:    adapterTSH
*                 targetTSCB
*
*  Remarks:
*
*********************************************************************/
#if SCSI_DOMAIN_VALIDATION
void SCSIxDvForceNegotiation (SCSI_ADAPTER_TSCB HIM_PTR adapterTSH,
                              SCSI_TARGET_TSCB HIM_PTR targetTSCB)
{
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   SCSI_DEVICE HIM_PTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetTSCB->scsiID];

   /* Make DV entry to be current negoXferIndex */
   deviceTable->negoXferIndex = SCSI_DV_ENTRY;

   /* force renegotiation */
   SCSI_FORCE_NEGOTIATION(&targetTSCB->unitControl);
}
#endif /* SCSI_DOMAIN_VALIDATION */

/*********************************************************************
*
*  SCSIxDvForceAsyncNegotiation
*
*     Force Async/Narrow Negotiation for domain validation    
*
*  Return Value:  none
*                  
*  Parameters:    adapterTSH
*                 targetTSCB
*
*  Remarks:
*
*********************************************************************/
#if (SCSI_DOMAIN_VALIDATION)
void SCSIxDvForceAsyncNegotiation (SCSI_ADAPTER_TSCB HIM_PTR adapterTSH,
                                   SCSI_TARGET_TSCB HIM_PTR targetTSCB)
{
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   SCSI_DEVICE HIM_PTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetTSCB->scsiID];

   /* Use temporary transfer entry to force async/narrow negotiation.  */
   /* Because the DV transfer entry is already updated to be a default */
   /* tranfer entry during SCSIxRestoreDefaultXfer routine.            */

   /* Force Narrow */
   SCSI_TEMP_XFER(deviceTable)[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;

   /* Force Async */
   SCSI_TEMP_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;
   
   SCSI_TEMP_XFER(deviceTable)[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
   SCSI_TEMP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] = 0;

   /* Make Temp entry to be the current negoXferIndex */
   deviceTable->negoXferIndex = SCSI_TEMP_ENTRY;

   /* force renegotiation */
   SCSI_FORCE_NEGOTIATION(&targetTSCB->unitControl);
}
#endif /* SCSI_DOMAIN_VALIDATION */

/*********************************************************************
*
*  SCSIxSetupDomainValidation
*
*     Setup for Domain Validation
*
*  Return Value:  none
*                  
*  Parameters:    adapterTSH
*
*  Remarks:       1. For PAC all target/lun 0 found in bus scan will 
*                    exercise Domain Validation at end of PAC.
*                 2. For probe the specified target will exercise
*                    Domain Validation only if it's lun 0.
*
*********************************************************************/
#if SCSI_DOMAIN_VALIDATION
void SCSIxSetupDomainValidation (SCSI_ADAPTER_TSCB HIM_PTR adapterTSH)
{
   SCSI_TARGET_TSCB HIM_PTR targetTSCB = &adapterTSH->targetTSCBBusScan;
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   SCSI_UINT16 i;
   SCSI_DEVICE HIM_PTR deviceTable;

   adapterTSH->SCSI_AF_dvInProgress = 1;

   /* setup ready for Domain Validation */
   for (i = 0; i < SCSI_MAXDEV; ++i)
   {
      if (adapterTSH->SCSI_AF_dvLevel(i) != SCSI_DV_DISABLE)
      {
         /* set dv state to ready */
         adapterTSH->SCSI_AF_dvState(i) = SCSI_DV_STATE_SEND_ASYNC_INQUIRY;
         adapterTSH->SCSI_AF_dvPassed(i) = 0;
         adapterTSH->SCSI_AF_dvFallBack(i) = 0;

         deviceTable = &SCSI_DEVICE_TABLE(hhcb)[i];
         if (SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] & SCSI_DUALEDGE)
         {
            adapterTSH->SCSI_AF_dvThrottle(i) = SCSI_DE_WIDE;
         }
         else
         {
            if (SCSI_TARGET_CAP_XFER(deviceTable)[SCSI_XFER_MISC_OPT] & SCSI_WIDE)
            {
               adapterTSH->SCSI_AF_dvThrottle(i) = SCSI_SE_WIDE;
            }
            else
            {
               adapterTSH->SCSI_AF_dvThrottle(i) = SCSI_SE_NARROW_REPEAT;

            }
         }
      }
   }

   /* Reset the retry counter */
   adapterTSH->retryBusScan = 0;

   /* refresh the bus scan state machine (start with target 0, lun 0) */
   OSDmemset(targetTSCB,0,sizeof(SCSI_TARGET_TSCB));

   /* setup targetTSCB */
   targetTSCB->typeTSCB = SCSI_TSCB_TYPE_TARGET;
   targetTSCB->adapterTSH = adapterTSH;

   /* start with scsi id 0 */
   targetTSCB->scsiID = 0;
}
#endif /* SCSI_DOMAIN_VALIDATION */

/*********************************************************************
*
*  SCSIxQueueDomainValidation
*
*     Queue request for Domain Validation
*
*  Return Value:  none
*
*  Parameters:    adapterTSH
*
*  Remarks:
*
*********************************************************************/
#if (SCSI_DOMAIN_VALIDATION)
void SCSIxQueueDomainValidation (SCSI_ADAPTER_TSCB HIM_PTR adapterTSH)
{
   SCSI_TARGET_TSCB HIM_PTR targetTSCB = &adapterTSH->targetTSCBBusScan;
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;

   if (adapterTSH->SCSI_AF_dvFrozen)
   {
      /* Only continue DV if not frozen. */
      /* DV will be continued when unfreeze received in
       * OSDAsyncEvent.
       */
      return;
   }
   
   /* Check out where we are in Domain Validation exercise */
   for (; targetTSCB->scsiID < SCSI_MAXDEV; ++targetTSCB->scsiID)
   {
      if ((adapterTSH->SCSI_AF_dvLevel(targetTSCB->scsiID) != SCSI_DV_DISABLE) &&
          (adapterTSH->SCSI_AF_dvState(targetTSCB->scsiID) != SCSI_DV_STATE_DONE))
      {
         SCSIxQueueDomainValidationSM(adapterTSH);        
         break;
      }
      else if (adapterTSH->SCSI_AF_dvLevel(targetTSCB->scsiID) != SCSI_DV_DISABLE &&
               adapterTSH->SCSI_AF_dvState(targetTSCB->scsiID) == SCSI_DV_STATE_DONE)
      {

         /* Clear resetSCSI flag after the Domain Validation is done so that */
         /* when the subsequent HIM_PROBE invoked for the same id, the       */
         /* suppress negotiation will not get called. */
         SCSI_DEVICE_TABLE(hhcb)[targetTSCB->scsiID].SCSI_DF_resetSCSI = 0;
      }
   }

   /* all devices has been exercised with Domain Validation, it's done */
   if (targetTSCB->scsiID == 16)
   {
      adapterTSH->iobProtocolAutoConfig->taskStatus = adapterTSH->pacIobStatus;
      adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
      adapterTSH->SCSI_AF_dvInProgress = 0;
      /* If OSM is frozen then unfreeze the OSM because the OSMEvent handling */
      /* is complete */
      if ((adapterTSH->SCSI_AF_osmFrozen) &&
          (adapterTSH->iobProtocolAutoConfig->function != HIM_PROBE))
      {
         /* Set OSM is Unfrozen flag */
         adapterTSH->SCSI_AF_osmFrozen = 0;
         /* Call OSM with Unfreeze Event. */
         OSMxEvent(adapterTSH->osmAdapterContext,HIM_EVENT_OSMUNFREEZE,(void HIM_PTR) 0);
      }
   }
}
#endif /* (SCSI_DOMAIN_VALIDATION) */

/*********************************************************************
*
*  SCSIxDomainValidationPatternGenerator
*
*     This routine returns a default pattern for use by 
*     Domain Validation when the OSM does not define one.
*
*  Return Value:  The byte value defined for index
*
*  Parameters:    index
*
*  Remarks:
*
*********************************************************************/
#if (SCSI_DOMAIN_VALIDATION)
SCSI_UEXACT8 SCSIxDomainValidationPatternGenerator (SCSI_UEXACT8 index)
{
   SCSI_UEXACT16 bitPattern = (SCSI_UEXACT16) 0x0001;

   if (index < 32)
   {
      return (index);
   }
   else if (index < 48)
   {
      return ((index & 0x02) ? (SCSI_UEXACT8)0xFF : (SCSI_UEXACT8)0x00);
   }
   else if (index < 64)
   {
      return ((index & 0x02) ? (SCSI_UEXACT8)0xAA : (SCSI_UEXACT8)0x55);
   }
   else 
   {
      /* 64 to 128 */

      if (index & 0x02) 
      {
         if (index & 0x01)
         {
            return ((SCSI_UEXACT8)((bitPattern << (index-64)/4) & 0xff));
         }
         else
         {
            return((SCSI_UEXACT8)(((bitPattern << (index-64)/4) & 0xff00) >> 8)); 
         }
      }
      else
      {
         return (0xff);
      }
   }
}
#endif /* ((SCSI_DOMAIN_VALIDATION)) */

/*********************************************************************
*
*  SCSIxQueueDomainValidationSM
*
*     This routine implements the 'Queue' portion of the 
*     Domain Validation State Machine.
*  
*     Implemented States
*
*        SCSI_DV_STATE_SEND_INQUIRY
*        SCSI_DV_STATE_SEND_REBD
*        SCSI_DV_STATE_SEND_WEB
*        SCSI_DV_STATE_SEND_REB
*        SCSI_DV_STATE_SEND_MODESENSE (future)        
*        SCSI_DV_STATE_SEND_MODESELECT (future)        
*
*  Return Value:  none
*
*  Parameters:    adapterTSH
*
*  Remarks:
*
*********************************************************************/
#if (SCSI_DOMAIN_VALIDATION)
void SCSIxQueueDomainValidationSM (SCSI_ADAPTER_TSCB HIM_PTR adapterTSH)
{
   SCSI_TARGET_TSCB HIM_PTR targetTSCB = &adapterTSH->targetTSCBBusScan;
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   HIM_IOB HIM_PTR iob;
   HIM_TS_SCSI HIM_PTR transportSpecific = (HIM_TS_SCSI HIM_PTR)adapterTSH->iobProtocolAutoConfig->transportSpecific;
   HIM_BUS_ADDRESS busAddress;
#if !SCSI_DCH_U320_MODE
   HIM_UEXACT32 uexact32Value;
#else
   HIM_UEXACT32 uexact32Length;
#endif /* !SCSI_DCH_U320_MODE */
   HIM_UEXACT8 HIM_PTR uexact8Pointer;
   HIM_UINT8 i;
   HIM_UEXACT8 nextState;
   HIM_BOOLEAN dvDataPatternPresent;
   HIM_UEXACT8 currentSGListSize = sizeof(HIM_BUS_ADDRESS);
   HIM_UEXACT8 busAddressSize;
#if SCSI_DOMAIN_VALIDATION_ENHANCED 
   HIM_UEXACT8 startOffset;
#endif /* SCSI_DOMAIN_VALIDATION_ENHANCED */
#if !SCSI_FAST_PAC
   SCSI_UINT8 probeFlag = (adapterTSH->iobProtocolAutoConfig->function == HIM_PROBE);
#endif /* !SCSI_FAST_PAC */
   HIM_UEXACT8 timervalue[2];
   
   busAddressSize = OSM_BUS_ADDRESS_SIZE;

   /* Initialize DV data phase timer */
   timervalue[0] = 0;
   timervalue[1] = SCSI_DV_TIMEOUT_VALUE;
   SCSI_WRITE_SCRATCH_RAM(hhcb,
                       (SCSI_UEXACT16) (SCSI_hDVTIMER_LO(hhcb)-SCSI_SCRATCH0),
                       (SCSI_UEXACT16) 2,
                       (SCSI_UEXACT8 SCSI_SPTR) &timervalue[0]);
                       
#if !SCSI_FAST_PAC
   if ((!probeFlag) && (transportSpecific))
   {
      /*
         Record the TargetID & Lun of current device being scanned.
         OSM can use this information to identify the device that failed during
         scsi bus scan which caused PAC completed with an error.
         This is not useful for SCSI_FAST_PAC implementation.
      */
      transportSpecific->scsiID = (HIM_UEXACT8) targetTSCB->scsiID;
      OSDmemset(transportSpecific->LUN,0,sizeof(transportSpecific->LUN));
      transportSpecific->LUN[1] = (HIM_UEXACT8) targetTSCB->lunID;
   }
#endif /* !SCSI_FAST_PAC */
#if ((OSM_BUS_ADDRESS_SIZE == 64) && SCSI_STANDARD_MODE)
   if (hhcb->SCSI_HF_SgBusAddress32 &&
       ((hhcb->firmwareMode == SCSI_FMODE_STANDARD_U320) ||
        (hhcb->firmwareMode == SCSI_FMODE_STANDARD_ENHANCED_U320)))
   {
      currentSGListSize = 4;
      busAddressSize = 32;
   }
#elif SCSI_DCH_U320_MODE
   /* DCH SCSI uses Generic SG List size */
   currentSGListSize = 8;
   busAddressSize = 32;
#endif /* ((OSM_BUS_ADDRESS_SIZE == 64) && SCSI_STANDARD_MODE) */

   /* --- Common Operations --- */
   /* The following code is common to all states */

   /* Check to see if an OSM-defined data pattern is present */

   if ((transportSpecific) &&
       (transportSpecific->dvPattern.bufferSize != 0))
   {
      dvDataPatternPresent = HIM_TRUE;
   }
   else
   {
      dvDataPatternPresent = HIM_FALSE;
   }

   /* get iob memory */
   iob = (HIM_IOB HIM_PTR)(((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreUnlocked) + SCSI_MORE_IOB);
   OSDmemset(iob,0,sizeof(HIM_IOB));

   /* setup iob reserved memory */
   iob->iobReserve.virtualAddress = (void HIM_PTR) (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreLocked) + SCSI_MORE_IOBRSV);
   iob->iobReserve.bufferSize = sizeof(SCSI_IOB_RESERVE);
   iob->iobReserve.busAddress = OSMxGetBusAddress(SCSI_ADPTSH(adapterTSH)->osmAdapterContext, HIM_MC_LOCKED,iob->iobReserve.virtualAddress);
   OSDmemset(iob->iobReserve.virtualAddress,0,sizeof(SCSI_IOB_RESERVE));

   /* Setup transport specific area for Domain Validation */
   /* Warning - if the transportSpecific area changes then code in
    * SCSIQueueIOB will need to be updated.
    */
   iob->transportSpecific = (void HIM_PTR) (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreUnlocked) + SCSI_MORE_TRANSPORT_SPECIFIC);
   OSDmemset(iob->transportSpecific,0,sizeof(HIM_TS_SCSI));
   ((HIM_TS_SCSI HIM_PTR)(iob->transportSpecific))->dvIOB = HIM_TRUE;
   
   /* setup/validate temporary target task set handle */
   SCSI_SET_UNIT_HANDLE(&SCSI_ADPTSH(adapterTSH)->hhcb,
         &targetTSCB->unitControl,(SCSI_UEXACT8)targetTSCB->scsiID,0);

   /* setup iob for command */
   iob->function = HIM_INITIATE_TASK;
   iob->taskSetHandle = (HIM_TASK_SET_HANDLE) targetTSCB;
   iob->flagsIob.autoSense = 1;
   iob->postRoutine = (HIM_POST_PTR) SCSIxPostDomainValidationSM;
   iob->targetCommand = (void HIM_PTR) (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreUnlocked) + SCSI_MORE_INQCDB);

   /* setup sense data */
   iob->errorData = (void HIM_PTR) (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreLocked) + SCSI_MORE_SNSDATA);
   iob->errorDataLength = SCSI_SIZE_SENSE_DATA;
   iob->taskAttribute = HIM_TASK_SIMPLE;
   
#if SCSI_PACKETIZED_IO_SUPPORT
   targetTSCB->targetAttributes.tagEnable = 
      (((SCSI_DEVICE_TABLE(hhcb)[targetTSCB->scsiID].SCSI_DF_disconnectEnable << targetTSCB->scsiID) &
         adapterTSH->tagEnable) != 0);
   if (targetTSCB->targetAttributes.tagEnable == 1)
   {
      ((HIM_TS_SCSI HIM_PTR)(iob->transportSpecific))->disallowDisconnect = HIM_FALSE;
   }
   else
   {
      ((HIM_TS_SCSI HIM_PTR)(iob->transportSpecific))->disallowDisconnect = HIM_TRUE;
   }
#else
   targetTSCB->targetAttributes.tagEnable = 0;
   ((HIM_TS_SCSI HIM_PTR)(iob->transportSpecific))->disallowDisconnect = HIM_TRUE;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */                  

   /* The CDB is set within the state machine */
   uexact8Pointer = (SCSI_UEXACT8 HIM_PTR) iob->targetCommand;

   for (i = 1; i < 10; i++)
   {
      uexact8Pointer[i] = (SCSI_UEXACT8) 0x00;
   }

   /* --- State Machine --- */

   while (!SCSI_POSTDV_STATE())
   {
      switch (SCSI_DV_STATE()) 
      {
         /*-------------------------- INQ --------------------------- */
         case SCSI_DV_STATE_SEND_ASYNC_INQUIRY: 
            /* Having a short timeout for this command is not required, so */
            /* don't enable it.  This is to work around devices with very  */
            /* slow Narrow/Async performance (12ms for the entire 32 bytes */
            /* of Inquiry data)                                            */
            /* From 160 Update */
            
            ((HIM_TS_SCSI HIM_PTR)(iob->transportSpecific))->dvIOB = HIM_FALSE;
         case SCSI_DV_STATE_SEND_INQUIRY: 
            {
               /* Build CDB */

               uexact8Pointer[0] = (SCSI_UEXACT8) 0x12;
               uexact8Pointer[4] = (SCSI_UEXACT8) SCSI_SIZE_INQUIRY_DVBASIC;

               iob->targetCommandLength = SCSI_INQ_CDB_SIZE;

               /* clear inquiry data buffer */
               for (i = 0; i < SCSI_SIZE_INQUIRY_DVBASIC; i++)
               {
                  ((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_INQDATA+i] = 0;
               }

               /* The SG List is built at the end of this routine */

               iob->data.virtualAddress = (void HIM_PTR)
                  (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreLocked) + SCSI_MORE_INQSG);

#if !SCSI_DCH_U320_MODE
               uexact32Value = (SCSI_UEXACT32) (0x80000000 | SCSI_SIZE_INQUIRY_DVBASIC);

#else
               uexact32Length = (SCSI_UEXACT32) SCSI_SIZE_INQUIRY_DVBASIC;
#endif /* !SCSI_DCH_U320_MODE */

               busAddress = OSMxGetBusAddress(
                  SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                  HIM_MC_LOCKED,
                  (void HIM_PTR)(((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)+
                  SCSI_MORE_INQDATA));

               if (SCSI_DV_STATE() == SCSI_DV_STATE_SEND_ASYNC_INQUIRY)
               {
                  SCSIxDvForceAsyncNegotiation(adapterTSH, targetTSCB);
                  nextState = SCSI_DV_STATE_ASYNC_INQ_OUTSTANDING;
#if SCSI_PACKETIZED_IO_SUPPORT           
                  targetTSCB->targetAttributes.tagEnable = 0;
                  ((HIM_TS_SCSI HIM_PTR)(iob->transportSpecific))->disallowDisconnect = HIM_TRUE;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */                  
                  
               }
               else
               {
                  /* Force Negotiation */
                  SCSIxDvForceNegotiation(adapterTSH, targetTSCB);
                  nextState = SCSI_DV_STATE_INQ_OUTSTANDING;
               }

               break;
            }

#if SCSI_DOMAIN_VALIDATION_ENHANCED

         case SCSI_DV_STATE_SEND_REBD: 
            {
               /* Build CDB */

               uexact8Pointer[0] = (SCSI_UEXACT8) 0x3C;
               uexact8Pointer[1] = (SCSI_UEXACT8) 0x0B;
               uexact8Pointer[8] = (SCSI_UEXACT8) SCSI_SIZE_REBD;

               iob->targetCommandLength = SCSI_RB_WB_CDB_SIZE;

               /* clear REBD data buffer */
               for (i = 0; i < SCSI_SIZE_REBD; i++)
               {
                  ((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+i] = 0;
               }

               /* Some of the SG List is built at the end of this routine */

               iob->data.virtualAddress = (void HIM_PTR)
                  (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreLocked) + SCSI_MORE_RWBSG);

#if !SCSI_DCH_U320_MODE
               uexact32Value = (SCSI_UEXACT32) (0x80000000 | SCSI_SIZE_REBD);
#else
               uexact32Length = (SCSI_UEXACT32) SCSI_SIZE_REBD;
#endif /* !SCSI_DCH_U320_MODE */

               busAddress = OSMxGetBusAddress(
                  SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                  HIM_MC_LOCKED,
                  (void HIM_PTR)(((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)+
                  SCSI_MORE_RWBDATA));

               /* Force Negotiation */
               SCSIxDvForceNegotiation(adapterTSH, targetTSCB);

               nextState = SCSI_DV_STATE_REBD_OUTSTANDING;

               break;
            }

         case SCSI_DV_STATE_SEND_WEB: 
            {
               /* Build CDB */

               uexact8Pointer[0] = (SCSI_UEXACT8) 0x3B;
               uexact8Pointer[1] = (SCSI_UEXACT8) 0x0A;

               /* clear REBD data buffer */
               for (i = 0; i < SCSI_SIZE_RWB; i++)
               {
                  ((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+i] = 0;
               }

               /* If header modes are enabled, insert a 4 byte header */
               /* identifying the initiator                           */

#if (SCSI_DV_MULTI_INITIATOR_MODE > SCSI_DV_TARGET_COLLISION_MODE)

               startOffset = SCSI_DV_HEADER_SIZE; /* header present */

               ((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+0] = SCSI_DV_ADPT_SIGNATURE;
               ((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+1] = hhcb->hostScsiID;
               ((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+2] = (SCSI_UEXACT8)(~(hhcb->hostScsiID));
               ((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+3] = (SCSI_UEXACT8)(~(SCSI_DV_ADPT_SIGNATURE));
#else
               startOffset = 0;
#endif /* (SCSI_DV_MULTI_INITIATOR_MODE > SCSI_DV_TARGET_COLLISION_MODE) */

               /* Maximum buffer size was determined in the REBD_OUTSTANDING
                  state. The actual buffer size may be reduced because the
                  amount of data to send is less than the maximum */

               if (dvDataPatternPresent != HIM_TRUE)
               {
                  adapterTSH->dvBufferSize = SCSI_SIZE_DEFAULT_RWB;
               }
               else 
               {
                  if (adapterTSH->dvBufferSize > ((SCSI_DV_PATTERN_LENGTH - adapterTSH->dvBufferOffset) + startOffset))
                  {
                     /* transfer the remainder */
                     adapterTSH->dvBufferSize = (SCSI_UEXACT8) (SCSI_DV_PATTERN_LENGTH - adapterTSH->dvBufferOffset) + startOffset;
                  }
               }
               
               uexact8Pointer[8] = (SCSI_UEXACT8) adapterTSH->dvBufferSize;

               iob->targetCommandLength = SCSI_RB_WB_CDB_SIZE;

               /* Initialize the data */

               /* dvBufferSize is set according to REBD result in Post SM */

               for (i=startOffset; i<adapterTSH->dvBufferSize; i++)
               {
                  ((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+i] = 
                     (dvDataPatternPresent == HIM_TRUE) ?

                  /* copy data from OSM-defined pattern, starting from 
                     BufferOffset, which is updated in the "post" portion
                     of the State Machine */

                  SCSI_DV_DATA_PATTERN(i+adapterTSH->dvBufferOffset - startOffset) :        
                  
                  /* Otherwise, No user defined pattern, use the 
                        standard one */

                  (SCSIxDomainValidationPatternGenerator((SCSI_UEXACT8)i));      
               }

               /* The SG List is built at the end of this routine */

               iob->data.virtualAddress = (void HIM_PTR)
                  (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreLocked) + SCSI_MORE_RWBSG);

#if !SCSI_DCH_U320_MODE
               uexact32Value = (SCSI_UEXACT32) (0x80000000 | adapterTSH->dvBufferSize);
#else
               uexact32Length = (SCSI_UEXACT32)adapterTSH->dvBufferSize;
#endif /* !SCSI_DCH_U320_MODE */

               busAddress = OSMxGetBusAddress(
                  SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                  HIM_MC_LOCKED,
                  (void HIM_PTR)(((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)+
                  SCSI_MORE_RWBDATA));

               /* Force Negotiation */
               SCSIxDvForceNegotiation(adapterTSH, targetTSCB);

               nextState = SCSI_DV_STATE_WEB_OUTSTANDING;

               break;
            }

         case SCSI_DV_STATE_SEND_REB: 
            {
               /* Build CDB */

               uexact8Pointer[0] = (SCSI_UEXACT8) 0x3C;
               uexact8Pointer[1] = (SCSI_UEXACT8) 0x0A;
               uexact8Pointer[8] = (SCSI_UEXACT8) adapterTSH->dvBufferSize;

               iob->targetCommandLength = SCSI_RB_WB_CDB_SIZE;

               /* clear REBD data buffer */
               for (i = 0; i < SCSI_SIZE_RWB; i++)
               {
                  ((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+i] = 0;
               }

               /* Some of the SG List is built at the end of this routine */

               iob->data.virtualAddress = (void HIM_PTR)
                  (((SCSI_UEXACT8 HIM_PTR) adapterTSH->moreLocked) + SCSI_MORE_RWBSG);

#if !SCSI_DCH_U320_MODE
               uexact32Value = (SCSI_UEXACT32) (0x80000000 | adapterTSH->dvBufferSize);
#else
               uexact32Length = (SCSI_UEXACT32) adapterTSH->dvBufferSize;
#endif /* !SCSI_DCH_U320_MODE */

               busAddress = OSMxGetBusAddress(
                  SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                  HIM_MC_LOCKED,
                  (void HIM_PTR)(((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)+
                  SCSI_MORE_RWBDATA));

               /* Force Negotiation */
               SCSIxDvForceNegotiation(adapterTSH, targetTSCB);

               nextState = SCSI_DV_STATE_REB_OUTSTANDING;

               break;
            }

#endif /* SCSI_DOMAIN_VALIDATION_ENHANCED */

      } /* end of switch */

      adapterTSH->SCSI_AF_dvState(targetTSCB->scsiID) = nextState;

   } /* end of while */

   /* Finish up with creating IOB */
   iob->data.bufferSize = 2 * currentSGListSize;
   iob->data.busAddress = OSMxGetBusAddress(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                              HIM_MC_LOCKED,iob->data.virtualAddress);

   /* setup sg element */
#if !SCSI_DCH_U320_MODE
   
#if ((OSM_BUS_ADDRESS_SIZE == 64) && SCSI_STANDARD_U320_MODE)
   SCSI_hPUTBUSADDRESSSG(hhcb,iob->data.virtualAddress,0,busAddress);
#else
   SCSI_hPUTBUSADDRESS(hhcb,iob->data.virtualAddress,0,busAddress);
#endif /* ((OSM_BUS_ADDRESS_SIZE == 64) && SCSI_STANDARD_U320_MODE) */

   HIM_PUT_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,iob->data.virtualAddress,
                           currentSGListSize,uexact32Value);

#else
   /* Changes for DCH support */
   HIM_PUT_LITTLE_ENDIAN32(hhcb,
                          iob->data.virtualAddress,
                          0,
                          busAddress);
   /*  pad zeros for 64 bit SG address */
   HIM_PUT_LITTLE_ENDIAN32(hhcb,
                          iob->data.virtualAddress, 
                          4,
                          0L);

   /* DCH uses common SG which supports uexact32 transfer length */
   HIM_PUT_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,iob->data.virtualAddress,
                           currentSGListSize,uexact32Length);
                           
   /* New for common SG List - set the 'LL' bit to indicate last list */ 
   /* Set ASEL bits to zero since memory allocation is internal to Rocket */ 
   HIM_PUT_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,iob->data.virtualAddress,
                           currentSGListSize+4,0x40000000);

#endif /* SCSI_DCH_U320_MODE) */

   HIM_FLUSH_CACHE(iob->data.virtualAddress,2*busAddressSize);

   /* execute it */
   SCSIQueueIOB(iob);
}
#endif /* SCSI_DOMAIN_VALIDATION */

/*****************************************************************/
/* Perform #define check to force error for ill-formed SCSIOSM.H */

#if SCSI_DOMAIN_VALIDATION

#ifndef SCSI_DV_MULTI_INITIATOR_MODE
#error SCSI_DV_MULTI_INITIATOR is undefined
#endif /* !SCSI_DV_MULTI_INITIATOR_MODE */

#ifndef SCSI_DV_TARGET_COLLISION_MODE
#error SCSI_DV_MULTI_INITIATOR mode #defines are missing
#endif /* !SCSI_DV_TARGET_COLLISION_MODE */

#endif /* SCSI_DOMAIN_VALIDATION */

/*                                                               */                                                            
/*****************************************************************/


/*********************************************************************
*
*  SCSIxPostDomainValidationSM
*
*     Post routine for Domain Validation level 2
*
*  Return Value:  status
*                  
*  Parameters:    iob
*
*  Remarks:
*
*********************************************************************/
#if (SCSI_DOMAIN_VALIDATION) 
HIM_UINT32 SCSIxPostDomainValidationSM(HIM_IOB HIM_PTR iob)
{
   SCSI_TARGET_TSCB HIM_PTR targetTSCB =
                              (SCSI_TARGET_TSCB HIM_PTR) iob->taskSetHandle;
   SCSI_UNIT_CONTROL HIM_PTR targetUnit = &targetTSCB->unitControl;
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = targetTSCB->adapterTSH;
   HIM_TS_SCSI HIM_PTR transportSpecific = (HIM_TS_SCSI HIM_PTR)adapterTSH->iobProtocolAutoConfig->transportSpecific;
   HIM_UINT8 residual;
   HIM_BOOLEAN dvDataPatternPresent;
   SCSI_UEXACT8 senseKey, asc, ascq;
   SCSI_UEXACT16 i, exceptionFlag;
   SCSI_UEXACT8  miscompareFlag = 0, startOffset = 0;
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   HIM_UEXACT8 nextState;
   SCSI_UEXACT32 sgLength;
#if SCSI_DCH_U320_MODE && (OSM_BUS_ADDRESS_SIZE == 32)
   SCSI_UEXACT8 currentSGListSize = sizeof(SCSI_BUS_ADDRESS)*2;
#else
   SCSI_UEXACT8 currentSGListSize = sizeof(SCSI_BUS_ADDRESS);
#endif /* (SCSI_DCH_U320_MODE && (OSM_BUS_ADDRESS_SIZE == 32)) */

#if (OSM_BUS_ADDRESS_SIZE == 64)
   if (hhcb->SCSI_HF_SgBusAddress32)
   {
      currentSGListSize = 4;
   }
#endif /* (OSM_BUS_ADDRESS_SIZE == 64) */


   /* Check to see if an OSM-defined data pattern is present */

   if ((transportSpecific) &&
       (transportSpecific->dvPattern.bufferSize != 0))
   {
      dvDataPatternPresent = HIM_TRUE;
   }
   else
   {
      dvDataPatternPresent = HIM_FALSE;
   }

   /* This routine is executed in response to the conclusion of an IOB  */
   /* Set Exception Flags based on IOB status                           */
   /* The flag may assume the following values

         - SCSI_DV_NO_EXCEPTION
         - SCSI_DV_ILLEGAL_REQUEST_EXCEPTION
         - SCSI_DV_BENIGN_CHECK_EXCEPTION
         - SCSI_DV_THROTTLING_CHECK_EXCEPTION
         - SCSI_DV_RESERVATION_CONFLICT_EXCEPTION
         - SCSI_DV_IOB_BUSY_EXCEPTION
         - SCSI_DV_CATASTROPHIC_EXCEPTION
         - SCSI_DV_DATA_ERROR_EXCEPTION
         - SCSI_DV_ECHO_BUFFER_OVERWRITTEN_EXCEPTION
         - SCSI_DV_DATA_UNDERRUN_EXCEPTION
         - SCSI_DV_OTHER_EXCEPTION                                     */

   switch (iob->taskStatus) 
   {
      case HIM_IOB_GOOD: 
         {
            exceptionFlag = SCSI_DV_NO_EXCEPTION;
            break;
         }

      case HIM_IOB_ERRORDATA_VALID:
         {
            HIM_GET_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,&senseKey,iob->errorData,2);

            if ((senseKey & 0x0F) == SCSI_SENSEKEY_ILLEGAL_REQUEST)
            {
               exceptionFlag = SCSI_DV_ILLEGAL_REQUEST_EXCEPTION;
            }
            else if (((senseKey & 0x0F) == SCSI_SENSEKEY_NOT_READY) || 
                     ((senseKey & 0x0F) == SCSI_SENSEKEY_RECOVERED_ERROR) ||
                     ((senseKey & 0x0F) == SCSI_SENSEKEY_UNIT_ATTENTION))
            {
               /* Benign errors are: UNIT_ATTENTION and NOT_READY */
               exceptionFlag = SCSI_DV_BENIGN_CHECK_EXCEPTION;
            }
            else if ((senseKey & 0x0F) == SCSI_SENSEKEY_ABORTED_COMMAND)
            {
               /* Check the ASC/ASCQ to determine if buffer was clobbered */

               HIM_GET_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,&asc,iob->errorData,12);
               HIM_GET_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,&ascq,iob->errorData,13);

               if ((asc == 0x3F) && (ascq == SCSI_ASCQ_ECHO_BUFFER_OVERWRITTEN))
               {
                  exceptionFlag = SCSI_DV_ECHO_BUFFER_OVERWRITTEN_EXCEPTION;
               }
               else
               {
                  exceptionFlag = SCSI_DV_THROTTLING_CHECK_EXCEPTION;
               }
            }
            else
            {
               /* Check to see if a data transfer has occurred. If it hasn't, then 
                  treat this as a benign error. Otherwise, force a throttle. The
                  way we determine whether a data transfer has occurred is by 
                  checking IOB->residual versus the SG list length */

               /* Obtain s/g element size from s/g list - note assumes
                * a single element list.
                */
               HIM_GET_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,
                                       &sgLength,
                                       iob->data.virtualAddress,
                                       currentSGListSize);
               /* Only 24 bits of length valid */
               sgLength &= 0x00FFFFFF;

               if (sgLength == iob->residual)
               {
                  if (adapterTSH->retryBusScan < SCSI_DV_BENIGN_RETRIES)
                  {
                     /* The original SG element matches residual, implying no data transfer */
                     exceptionFlag = SCSI_DV_BENIGN_CHECK_EXCEPTION;
                  }
                  else
                  {
                      /* Exhausted retries - throttle if possible. */
                      exceptionFlag = SCSI_DV_THROTTLING_CHECK_EXCEPTION;
                  }
               }
               else
               {    
                  exceptionFlag = SCSI_DV_THROTTLING_CHECK_EXCEPTION;
               }
            }
            break;
         }

      case HIM_IOB_TARGET_RESERVED:
         {
            exceptionFlag = SCSI_DV_RESERVATION_CONFLICT_EXCEPTION;
            break;            
         }

      case HIM_IOB_DATA_OVERUNDERRUN:
         {
            exceptionFlag = SCSI_DV_DATA_UNDERRUN_EXCEPTION;
            break;            
         }

      case HIM_IOB_BUSY:
      case HIM_IOB_TASK_SET_FULL:
         {
            /* must free the current unit handle */
            SCSI_FREE_UNIT_HANDLE(*((SCSI_UNIT_HANDLE HIM_PTR)&targetUnit));
         
            /* Unfreeze the queues */
            SCSIxQueueSpecialIOB(adapterTSH, HIM_UNFREEZE_QUEUE);

            exceptionFlag = SCSI_DV_IOB_BUSY_EXCEPTION;
            break;
         }

      case HIM_IOB_ABORTED_REQ_BUS_RESET:
      case HIM_IOB_ABORTED_CHIM_RESET:
      case HIM_IOB_ABORTED_TRANSPORT_MODE_CHANGE:
      case HIM_IOB_ABORTED_3RD_PARTY_RESET:

#if !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING
      case HIM_IOB_ABORTED_PCI_ERROR:
      case HIM_IOB_ABORTED_PCIX_SPLIT_ERROR:
      case HIM_IOB_PCI_OR_PCIX_ERROR:
#endif /* !SCSI_DISABLE_PCI_PCIX_ERROR_HANDLING */
      
         {
            exceptionFlag = SCSI_DV_CATASTROPHIC_EXCEPTION;
            break;
         }

      case HIM_IOB_HOST_ADAPTER_FAILURE:
         {
            exceptionFlag = SCSI_DV_DATA_ERROR_EXCEPTION;
            break;
         }

      default:
         {
            exceptionFlag = SCSI_DV_OTHER_EXCEPTION;
            break;
         }
   }

   /* catastrophic errors always result in the same action, so handle them */
   /* outside the state machine                                            */

   if (exceptionFlag == SCSI_DV_CATASTROPHIC_EXCEPTION)
   {
      /* abort protocol auto config because of a catastrophic event */
      /* post protocol auto config back to osm */

      adapterTSH->iobProtocolAutoConfig->taskStatus = iob->taskStatus;
      adapterTSH->iobProtocolAutoConfig->postRoutine(adapterTSH->iobProtocolAutoConfig);
      adapterTSH->SCSI_AF_dvInProgress = 0;
      
      return(HIM_SUCCESS);
   }

   /* The DV State machine is partitioned into pre-command and post-command 
      states.  The following code segment implements the post-command states.
      If it transitions to a pre-command state (indicating a SCSI command will
      be sent), it will call SCSIhQueueDomainValidationSM, then exit this
      routine. */

   while (SCSI_POSTDV_STATE())
   {
      switch (SCSI_DV_STATE()) 
      {

#if SCSI_DOMAIN_VALIDATION_ENHANCED

         /*-------------------------- REBD --------------------------- */
         case SCSI_DV_STATE_REBD_OUTSTANDING: 
            {
               /* The Queue portion of this state machine has queued a Read Echo
                  Buffer Descriptor */

               /* --- Error Transitions */

               if ((exceptionFlag & (SCSI_DV_BENIGN_CHECK_EXCEPTION |
                                     SCSI_DV_RESERVATION_CONFLICT_EXCEPTION |
                                     SCSI_DV_IOB_BUSY_EXCEPTION)))
               {
                  if (adapterTSH->retryBusScan < SCSI_DV_BENIGN_RETRIES)
                  {
                     /* It is OK to retry */
                     adapterTSH->retryBusScan++;
                     nextState = SCSI_DV_STATE_SEND_REBD;
                  }
                  else
                  {
                     /* retry count exhausted, fail if an error */
                     adapterTSH->retryBusScan = 0;
                     nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                  }
               } 
               else if (exceptionFlag & (SCSI_DV_THROTTLING_CHECK_EXCEPTION |
                                         SCSI_DV_DATA_ERROR_EXCEPTION       |
                                         SCSI_DV_DATA_UNDERRUN_EXCEPTION    |
                                         SCSI_DV_OTHER_EXCEPTION))
               {
                  /* Throttle, and retry if possible */
                  adapterTSH->retryBusScan = 0;
                  if (!SCSIxThrottleTransfer(adapterTSH, targetTSCB))
                  {                  
                     /* throttle successfull */
                     nextState = SCSI_DV_STATE_SEND_REBD;
                  }
                  else
                  {
                     /* already at minimum speed, can't throttle anymore */
                     if (adapterTSH->iobProtocolAutoConfig->function == HIM_PROBE)
                     {
                        /* Only return this taskStatus for HIM_PROBE */
                        adapterTSH->pacIobStatus = HIM_IOB_DOMAIN_VALIDATION_FAILED;
                     }

                     SCSIxClearLunExist(adapterTSH->lunExist,targetTSCB->scsiID,targetTSCB->lunID);

                     nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                  }
               }
               else if (exceptionFlag == SCSI_DV_ILLEGAL_REQUEST_EXCEPTION)
               {
                  /* Target does not support REBD, assume a size */

#if (SCSI_DV_MULTI_INITIATOR_MODE > SCSI_DV_TARGET_COLLISION_MODE) || (SCSI_DV_MULTIINITIATOR == 0)
                  /* Default is OK if there are other mechanisms for 
                     collision detection */
                  adapterTSH->dvBufferSize = SCSI_SIZE_DEFAULT_RWB;
                  adapterTSH->retryBusScan = 0;
                  nextState = SCSI_DV_STATE_SEND_WEB;
#else
                  /* If REBD is not supported, it is virtually certain
                     that target-detected collisions aren't either. Since
                     no other collision detection modes are enabled, go
                     the safe route and stick to Level 1 results. */

                  adapterTSH->retryBusScan = 0;
   
                  /* Force Negotiation */
                  SCSIxDvForceNegotiation(adapterTSH, targetTSCB);
                  nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
#endif /* (SCSI_DV_MULTI_INITIATOR_MODE > SCSI_DV_TARGET_COLLISION_MODE) || (SCSI_DV_MULTIINITIATOR == 0) */
               }
               else 
               {
                  /* We had a good completion. Analyze Data. */

                  /* Check the EBOS bit. If no other collision modes are
                     enabled, don't use Level 2 DV */
#if ((SCSI_DV_MULTI_INITIATOR_MODE < SCSI_DV_LOOSE_HEADER_MODE) && (SCSI_DV_MULTIINITIATOR == 1))

                  if ((((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA] & SCSI_DV_EBOS) != SCSI_DV_EBOS)
                  {
                     adapterTSH->retryBusScan = 0;
   
                     /* Force Negotiation */
                     SCSIxDvForceNegotiation(adapterTSH, targetTSCB);
                     nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                     break;
                  }
#endif /* (SCSI_DV_MULTI_INITIATOR_MODE == SCSI_DV_TARGET_COLLISION_MODE) && (SCSI_DV_MULTIINITIATOR == 1) */

/*
 Compaq reported a particular drive vendor which would lockup during the
 Netware 5.1 loading.  The vendor ultimatly had to make a code change to their
 drive to make a change to their Echo Buffer Capacity changing it from 0200h
 to 00FCh bytes. Afterwards the failure no longer occurs.  Fixed the
 implementation for checking the echo buffer size in BUFFER CAPACITY field.
 Prevoius checking is for the lower 5 bits of offset 3 which only contains
 the size up to FFh.  The new fix is to check the lower 5 bits of offset 2
 if not zero, then set dvbuffersize=128 bytes; otherwise,
 dvbuffersize=the value in offset 3 in BUFFER CAPACITY field.
 ftp://ftp.t10.org/t10/drafts/spc2/spc2r19.pdf See pp132. */

                  if ((((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+2] && 0x1F) != 0)
                  {
                     /* The echo buffer is larger than 256 bytes, round down for now */
                     adapterTSH->dvBufferSize = 128;
                  }
                  else
                  {   
                     adapterTSH->dvBufferSize = 
                        ((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+3];
                  }

                  /* Round down to the nearest 'power of two' to simplify */
                  /* design of data patterns */

                  if (adapterTSH->dvBufferSize > 128)
                  {
                     adapterTSH->dvBufferSize = 128;
                  }
                  else if (adapterTSH->dvBufferSize > 64)
                  {
                     adapterTSH->dvBufferSize = 64;
                  }
                  else if (adapterTSH->dvBufferSize > 32)
                  {
                     adapterTSH->dvBufferSize = 32;
                  }
                  else if (adapterTSH->dvBufferSize > 16)
                  {
                     adapterTSH->dvBufferSize = 16;
                  }
                  else
                  { 
                     /* it is probably not meaningfull to do Level 2 with less
                        than 16 bytes, so stick with Level 1 results */
                     adapterTSH->retryBusScan = 0;
   
                     /* Force Negotiation */
                     SCSIxDvForceNegotiation(adapterTSH, targetTSCB);

                     nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                     break;
                  }

                  adapterTSH->retryBusScan = 0;
                  nextState = SCSI_DV_STATE_SEND_WEB;
               }

               adapterTSH->dvReportedBufferSize = adapterTSH->dvBufferSize;
               adapterTSH->dvBufferOffset = 0;
               adapterTSH->retryDVBufferConflict = 0;

               break;
            }

         /*-------------------------- WEB --------------------------- */
         case SCSI_DV_STATE_WEB_OUTSTANDING: 
            {
               /* The Queue portion of this state machine has queued a 
                  Write Echo Buffer */
               /* --- Error Transitions */

               if ((exceptionFlag & (SCSI_DV_BENIGN_CHECK_EXCEPTION |
                                     SCSI_DV_RESERVATION_CONFLICT_EXCEPTION |
                                     SCSI_DV_IOB_BUSY_EXCEPTION)))
               {
                  if (adapterTSH->retryBusScan < SCSI_DV_BENIGN_RETRIES)
                  {
                     /* It is OK to retry */
                     adapterTSH->retryBusScan++;
                     nextState = SCSI_DV_STATE_SEND_WEB;
                  }
                  else
                  {
                     /* retry count exhausted, fail if an error */
                     adapterTSH->retryBusScan = 0;
                     nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                  }
               } 
               else if (exceptionFlag & (SCSI_DV_THROTTLING_CHECK_EXCEPTION |
                                         SCSI_DV_DATA_ERROR_EXCEPTION       |
                                         SCSI_DV_DATA_UNDERRUN_EXCEPTION    |
                                         SCSI_DV_OTHER_EXCEPTION))
               {
                  /* Throttle, and retry if possible */
                  adapterTSH->retryBusScan = 0;
                  if (!SCSIxThrottleTransfer(adapterTSH, targetTSCB))
                  {                  
                     /* throttle successfull */
                     adapterTSH->dvBufferOffset = 0;
                     adapterTSH->dvBufferSize = adapterTSH->dvReportedBufferSize;
                     nextState = SCSI_DV_STATE_SEND_WEB;
                  }
                  else
                  {
                     /* already at minimum speed, can't throttle anymore */
                     if (adapterTSH->iobProtocolAutoConfig->function == HIM_PROBE)
                     {    
                        adapterTSH->pacIobStatus = HIM_IOB_DOMAIN_VALIDATION_FAILED;
                     }
                     SCSIxClearLunExist(adapterTSH->lunExist,targetTSCB->scsiID,targetTSCB->lunID);
                     nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                  }
               } 
               else if (exceptionFlag == SCSI_DV_ILLEGAL_REQUEST_EXCEPTION)
               {
                  /* This device does not support Echo Buffer */
                  adapterTSH->retryBusScan = 0;
   
                  /* Force Negotiation */
                  SCSIxDvForceNegotiation(adapterTSH, targetTSCB);

                  nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
               }

               /* --- Acceptable response, go to REB */
               else
               {
                  adapterTSH->retryBusScan = 0;
                  nextState = SCSI_DV_STATE_SEND_REB;
               }
            
               break;
            }

         /*-------------------------- REB ---------------------------*/
         case SCSI_DV_STATE_REB_OUTSTANDING: 
            {
               /* The Queue portion of this state machine has queued a 
                  Read Echo Buffer */
 
               miscompareFlag = 0;

               /* --- Error Transitions */

               if ((exceptionFlag & (SCSI_DV_BENIGN_CHECK_EXCEPTION |
                                     SCSI_DV_RESERVATION_CONFLICT_EXCEPTION |
                                     SCSI_DV_IOB_BUSY_EXCEPTION)))
               {
                  if (adapterTSH->retryBusScan < SCSI_DV_BENIGN_RETRIES)
                  {
                     /* It is OK to retry */
                     adapterTSH->retryBusScan++;
                     nextState = SCSI_DV_STATE_SEND_REB;
                  }
                  else
                  {
                     /* retry count exhausted, fail if an error */
                     adapterTSH->retryBusScan = 0;
                     nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                  }
               } 
               else if (exceptionFlag & (SCSI_DV_THROTTLING_CHECK_EXCEPTION |
                                         SCSI_DV_DATA_ERROR_EXCEPTION       |
                                         SCSI_DV_OTHER_EXCEPTION))
               {
                  /* Throttle, and retry if possible */
                  adapterTSH->retryBusScan = 0;
                  if (!SCSIxThrottleTransfer(adapterTSH, targetTSCB))
                  {                  
                     /* throttle successfull */
                     adapterTSH->dvBufferOffset = 0;
                     adapterTSH->dvBufferSize = adapterTSH->dvReportedBufferSize;
                     nextState = SCSI_DV_STATE_SEND_WEB;
                  }
                  else
                  {
                     /* already at minimum speed, can't throttle anymore */
                     if (adapterTSH->iobProtocolAutoConfig->function == HIM_PROBE)
                     {
                        /* Only return this taskStatus for HIM_PROBE */
                        adapterTSH->pacIobStatus = HIM_IOB_DOMAIN_VALIDATION_FAILED;
                     }

                     SCSIxClearLunExist(adapterTSH->lunExist,targetTSCB->scsiID,targetTSCB->lunID);
                     nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                  }
               } 
               else if (exceptionFlag == SCSI_DV_ECHO_BUFFER_OVERWRITTEN_EXCEPTION)
               {
                  /* We were clobbered by another initiator, try again */
                  /* after an ID-specific Holdoff period               */
                  adapterTSH->retryBusScan = 0;

                  if (adapterTSH->retryDVBufferConflict < SCSI_DV_BUFFER_CONFLICT_RETRIES)
                  {
                     /* It is OK to retry */
                     adapterTSH->retryDVBufferConflict++;

                     /* Wait (1ms * my ID) */
                     for (i = 0; i < (SCSI_UEXACT16)(hhcb->hostScsiID)*200; i++)
                     {
                        OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
                     }

                     nextState = SCSI_DV_STATE_SEND_WEB;
                  }
                  else
                  {
                     /* retry count exhausted, fail if an error */
                     adapterTSH->retryDVBufferConflict = 0;
                     nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                  }
               }
               else if (exceptionFlag == SCSI_DV_ILLEGAL_REQUEST_EXCEPTION)
               {
                  /* This device does not support Echo Buffer */
                  adapterTSH->retryBusScan = 0;
                  nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
               }
               else
               {
                  /* The command completed without error. Analyze the returned
                     data to detect miscompares */

#if (SCSI_DV_MULTI_INITIATOR_MODE > SCSI_DV_TARGET_COLLISION_MODE)

                  startOffset = SCSI_DV_HEADER_SIZE; /* header present */

                  /* Check if the header matches */

                  if (!((((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+0] == SCSI_DV_ADPT_SIGNATURE) &&
                        (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+1] == (hhcb->hostScsiID))   &&
                        (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+2] == (SCSI_UEXACT8)~(hhcb->hostScsiID))  &&
                        (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+3] == (SCSI_UEXACT8)~(SCSI_DV_ADPT_SIGNATURE))))
                  {
                     /* didn't match, check further depending on mode */

#if (SCSI_DV_MULTI_INITIATOR_MODE > SCSI_DV_LOOSE_HEADER_MODE)

                     /* If Signature is correct, and 
                        SCSI ID doesn't match, and
                        Byte 3 = ~Byte 2.
                        Then we almost certainly have an overwrite condition
                        Otherwise it is a data mismatch */

                     if ((adapterTSH->retryDVBufferConflict < SCSI_DV_BUFFER_CONFLICT_RETRIES) && /* dont bother if we cant retry anyway */
                         (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+0] == SCSI_DV_ADPT_SIGNATURE) &&
                         (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+3] == (SCSI_UEXACT8)~(SCSI_DV_ADPT_SIGNATURE)) &&
                         (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+1] != (hhcb->hostScsiID)) &&
                         (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+2] == 
                            (SCSI_UEXACT8)~(((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+1])))
                     {
                        /* Its safe to assume we have an overwrite condition */

                        /* The signature is valid, but the ID isn't, so we can
                           assume another Adaptec Initiator has clobbered our
                           last WEB. Since we're nice guys, lets retry if we
                           can */
                        miscompareFlag |= SCSI_DV_HEADER_MISCOMPARE;
                     }
                     else
                     {
                        /* We can't retry anymore, or we have a header mismatch */
                        miscompareFlag |= SCSI_DV_DATA_MISCOMPARE;
                     }

#else /* !(SCSI_DV_MULTI_INITIATOR_MODE > SCSI_DV_LOOSE_HEADER_MODE) */

                     if ((adapterTSH->retryDVBufferConflict < SCSI_DV_BUFFER_CONFLICT_RETRIES) && /* dont bother if we cant retry anyway */
                         (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+0] != SCSI_DV_ADPT_SIGNATURE) ||
                         (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+1] != (hhcb->hostScsiID))   ||
                         (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+2] != (SCSI_UEXACT8)~(hhcb->hostScsiID))  ||
                         (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+3] != (SCSI_UEXACT8)~(SCSI_DV_ADPT_SIGNATURE)))
                     {
                        /* By comparing our header, we've determined someone 
                           else has clobbered our last Write Buffer. We need
                           to retry the Write Buffer after a holdoff period */
                        miscompareFlag |= SCSI_DV_HEADER_MISCOMPARE;
                     }
                     else
                     {
                        miscompareFlag |= SCSI_DV_DATA_MISCOMPARE;
                     }
#endif /* (mode 2) */

                  } /* end of header compare 'if' */

#else  /* (mode 1) */
                  startOffset = 0;
#endif /* (SCSI_DV_MULTI_INITIATOR_MODE > SCSI_DV_TARGET_COLLISION_MODE) */

                  /* Only check the data pattern if there hasn't been 
                     a header miscompare */
                  if (miscompareFlag == 0)
                  {
                     for (i=startOffset; i<adapterTSH->dvBufferSize; i++)
                     {
                        /* Compare the incoming data against the source    */

                        /* dvBufferSize gets set to the size of the actual */
                        /* transfer in the SEND_WEB state                  */

                        /* For Write/Read operations requiring more than   */
                        /* one cycle, all cycles will use the max size     */
                        /* except the last one                             */

                        /* If a data pattern was provided by the OSM,      */
                        /* BufferOffset points to the location in the user */
                        /* defined pattern where we copied from last. It   */
                        /* is initialized in the REBD_Oustanding state     */
                        /* then updated below                              */
                  
                        if (( (dvDataPatternPresent == HIM_TRUE) &&
                              (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+i] !=
                                (SCSI_DV_DATA_PATTERN(i + adapterTSH->dvBufferOffset - startOffset) ) ))
                            ||

                            ( (dvDataPatternPresent != HIM_TRUE) &&
                              (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreLocked)[SCSI_MORE_RWBDATA+i] !=
                                (SCSIxDomainValidationPatternGenerator((SCSI_UEXACT8)i)))))
                        {
                           miscompareFlag |= SCSI_DV_DATA_MISCOMPARE;
                           break;  /* out of for loop */
                        }                    
                     }
                  }
                        
#if (SCSI_DV_MULTI_INITIATOR_MODE > SCSI_DV_TARGET_COLLISION_MODE)
                  if ((miscompareFlag & SCSI_DV_HEADER_MISCOMPARE) == SCSI_DV_HEADER_MISCOMPARE)
                  {
                     /* Only Header Miscompare, so hold off and retry */

                     adapterTSH->retryBusScan = 0;

                     if (adapterTSH->retryDVBufferConflict < SCSI_DV_BUFFER_CONFLICT_RETRIES)
                     {
                        /* It is OK to retry */
                        adapterTSH->retryDVBufferConflict++;

                        /* Wait (1ms * my ID) */
                        for (i = 0; i < (SCSI_UEXACT16)(hhcb->hostScsiID)*200; i++)
                        {
                           OSD_DELAY(hhcb,(SCSI_UINT32)SCSI_MINIMUM_DELAY_COUNT);
                        }

                        nextState = SCSI_DV_STATE_SEND_WEB;
                     }
                     else
                     {
                        /* retry count exhausted, fail if an error */
                        adapterTSH->retryDVBufferConflict = 0;
                        nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                     }
                     break;
                  }
                  else
#endif /* (SCSI_DV_MULTI_INITIATOR_MODE > SCSI_DV_TARGET_COLLISION_MODE) */
                  if ((miscompareFlag & SCSI_DV_DATA_MISCOMPARE) == SCSI_DV_DATA_MISCOMPARE)
                  {
                     /* Data Miscompare, throttle down and retry */

                     adapterTSH->retryBusScan = 0;
                     adapterTSH->retryDVBufferConflict = 0;

                     /* When we throttle, we have to restart the DV */
                     /* at the beginning of the transfer            */

                     adapterTSH->dvBufferOffset = 0;
                     adapterTSH->dvBufferSize = adapterTSH->dvReportedBufferSize;
                     
                     if (!SCSIxThrottleTransfer(adapterTSH, targetTSCB))
                     {                  
                        /* throttle successfull */
                        nextState = SCSI_DV_STATE_SEND_WEB;
                     }
                     else
                     {
                        /* already at minimum speed, can't throttle anymore */
                        if (adapterTSH->iobProtocolAutoConfig->function == HIM_PROBE)
                        {
                           /* Only return this taskStatus for HIM_PROBE */
                           adapterTSH->pacIobStatus = HIM_IOB_DOMAIN_VALIDATION_FAILED;
                        }

                        SCSIxClearLunExist(adapterTSH->lunExist,targetTSCB->scsiID,targetTSCB->lunID);
                        nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                     }
                  }
                  else             
                  {
                     /* No miscompares */

                     /* If we've exhausted the data pattern, we're done: post Success */
                     /* Otherwise, we need to proceed to the next chunk of data       */

                     if ((dvDataPatternPresent == HIM_TRUE) &&
                         ((SCSI_UEXACT32)(adapterTSH->dvBufferOffset + adapterTSH->dvBufferSize - startOffset) < SCSI_DV_PATTERN_LENGTH))
                     {
                        /* We have more to go - we never come here if     */
                        /* a data pattern has not been defined by the OSM */
                        
                        adapterTSH->dvBufferOffset += (adapterTSH->dvBufferSize - startOffset);

                        adapterTSH->retryDVBufferConflict = 0;
                        adapterTSH->retryBusScan = 0;
                        nextState = SCSI_DV_STATE_SEND_WEB;                        
                     }
                     else
                     {
                        /* This device has concluded DV successfully */
                        adapterTSH->retryBusScan = 0;
                        adapterTSH->SCSI_AF_dvPassed(targetTSCB->scsiID) = 1;               
                        nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                     }
                  }
               } /* command complete branch */
            
               break;

            } /* end of REB case */

#endif /* #if SCSI_DOMAIN_VALIDATION_ENHANCED */
            
         /*-------------------------- INQ ---------------------------*/
         case SCSI_DV_STATE_ASYNC_INQ_OUTSTANDING: 
         case SCSI_DV_STATE_INQ_OUTSTANDING: 
            {
               /* The Queue portion of this state machine has queued an 
                  Inquiry command */
 
               miscompareFlag = HIM_FALSE;

               /* --- Error Transitions */

               if ((exceptionFlag & (SCSI_DV_BENIGN_CHECK_EXCEPTION |
                                     SCSI_DV_RESERVATION_CONFLICT_EXCEPTION |
                                     SCSI_DV_ILLEGAL_REQUEST_EXCEPTION |
                                     SCSI_DV_IOB_BUSY_EXCEPTION)))
               {
                  if (adapterTSH->retryBusScan < SCSI_DV_BENIGN_RETRIES)
                  {
                     /* It is OK to retry */
                     adapterTSH->retryBusScan++;
                     nextState = (SCSI_DV_STATE() == SCSI_DV_STATE_INQ_OUTSTANDING)? 
                        SCSI_DV_STATE_SEND_INQUIRY : SCSI_DV_STATE_SEND_ASYNC_INQUIRY;
                  }
                  else
                  {
                     /* retry count exhausted, fail if an error */
                     adapterTSH->retryBusScan = 0;
                     nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                  }

               } 
               else if (exceptionFlag & (SCSI_DV_THROTTLING_CHECK_EXCEPTION |
                                         SCSI_DV_DATA_ERROR_EXCEPTION       |
                                         SCSI_DV_OTHER_EXCEPTION))
               {
                  /* Throttle, and retry if possible */
                  adapterTSH->retryBusScan = 0;
                  if (SCSI_DV_STATE() == SCSI_DV_STATE_ASYNC_INQ_OUTSTANDING)
                  {
                     /* you can't throttle Async INQ, so go to next target */
                     if (adapterTSH->iobProtocolAutoConfig->function == HIM_PROBE)
                     {    
                        adapterTSH->pacIobStatus = HIM_IOB_DOMAIN_VALIDATION_FAILED;
                     }
                     SCSIxClearLunExist(adapterTSH->lunExist,targetTSCB->scsiID,targetTSCB->lunID);                           
                     nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;    
                  }
                  else if (!SCSIxThrottleTransfer(adapterTSH, targetTSCB))
                  {                  
                     /* throttle successfull */
                     nextState = SCSI_DV_STATE_SEND_INQUIRY;
                  }
                  else
                  {
                     /* already at minimum speed, can't throttle anymore */
                     if (adapterTSH->iobProtocolAutoConfig->function == HIM_PROBE)
                     {    
                        adapterTSH->pacIobStatus = HIM_IOB_DOMAIN_VALIDATION_FAILED;
                     }
                     SCSIxClearLunExist(adapterTSH->lunExist,targetTSCB->scsiID,targetTSCB->lunID);
                     nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                  }
               } 
               else
               {
                  /* The command completed without error. Analyze the returned
                     data to detect miscompares */

                  if (exceptionFlag == SCSI_DV_DATA_UNDERRUN_EXCEPTION)
                  {
                     residual = (HIM_UINT8)iob->residual;
                  }
                  else
                  {
                     residual = 0;
                  }

                  for (i = 0; i < (SCSI_UEXACT8)(SCSI_SIZE_INQUIRY_DVBASIC - residual); i++)
                  {
                     if ((((SCSI_UEXACT8 HIM_PTR )adapterTSH->moreUnlocked) + SCSI_MORE_REFDATA + targetTSCB->scsiID * SCSI_SIZE_INQUIRY)[i] !=
                        (((SCSI_UEXACT8 HIM_PTR )adapterTSH->moreLocked) + SCSI_MORE_INQDATA)[i])
                     {
                        /* Data Miscompare, throttle down and retry */

                        miscompareFlag = HIM_TRUE;

                        adapterTSH->retryBusScan = 0;

                        if (SCSI_DV_STATE() == SCSI_DV_STATE_ASYNC_INQ_OUTSTANDING)
                        {
                           /* you can't throttle Async INQ, so go to next target */
                           if (adapterTSH->iobProtocolAutoConfig->function ==
                               HIM_PROBE)
                           {    
                              adapterTSH->pacIobStatus =
                                 HIM_IOB_DOMAIN_VALIDATION_FAILED;
                           }
                           SCSIxClearLunExist(adapterTSH->lunExist,targetTSCB->scsiID,targetTSCB->lunID);                           
                           nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;    
                        }
                        else if (!SCSIxThrottleTransfer(adapterTSH, targetTSCB))
                        {                                          
                           /* throttle successfull, retry */                           
                           nextState = SCSI_DV_STATE_SEND_INQUIRY;                          
                        }                        
                        else                        
                        {                        
                           /* already at minimum speed, can't throttle anymore */                           
                           if (adapterTSH->iobProtocolAutoConfig->function ==
                               HIM_PROBE)
                           {    
                              adapterTSH->pacIobStatus =
                                 HIM_IOB_DOMAIN_VALIDATION_FAILED;
                           }
                           SCSIxClearLunExist(adapterTSH->lunExist,targetTSCB->scsiID,targetTSCB->lunID);                           
                           nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;                           
                        }
                      
                        break;                     
                     }
                  }

                  if (miscompareFlag != HIM_TRUE)
                  {
                     /* successfull Basic DV, go on to enhanced if supported */

                     adapterTSH->retryBusScan = 0;

                     if (adapterTSH->SCSI_AF_dvLevel(targetTSCB->scsiID) == SCSI_DV_ENHANCED)
                     {
                        if (SCSI_DV_STATE() == SCSI_DV_STATE_INQ_OUTSTANDING)
                        {
                           nextState = SCSI_DV_STATE_SEND_REBD;
                        }
                        else
                        {
                           nextState = SCSI_DV_STATE_SEND_INQUIRY;
                        } 
                     }
                     else
                     {
                        if (SCSI_DV_STATE() == SCSI_DV_STATE_INQ_OUTSTANDING)
                        {
                           adapterTSH->SCSI_AF_dvPassed(targetTSCB->scsiID) = 1;
                           nextState = SCSI_DV_STATE_GET_NEXT_DEVICE;
                        }   
                        else
                        {
                           /* assumes previous state was 
                            * SCSI_DV_STATE_ASYNC_INQ_OUTSTANDING
                            */
                           nextState = SCSI_DV_STATE_SEND_INQUIRY;                   
                        }
                     }
                  }

               } /* command complete branch */
            
               break;

            } /* end of INQ case */

         case SCSI_DV_STATE_GET_NEXT_DEVICE: 
            {
               /* the following routine will look for the next target */            
               adapterTSH->SCSI_AF_dvState(targetTSCB->scsiID) = SCSI_DV_STATE_DONE;
               SCSIxQueueDomainValidation(adapterTSH);
               return(HIM_SUCCESS);
            }
            
      } /* end of State Machine Switch */

      adapterTSH->SCSI_AF_dvState(targetTSCB->scsiID) = nextState;

   } /* end of while loop */

   /* If we're here, it is time to switch to the Queue portion of the */
   /* state machine                                                   */

   SCSIxQueueDomainValidation(adapterTSH);
   return(HIM_SUCCESS);
}
#endif /* (SCSI_DOMAIN_VALIDATION) */

/*********************************************************************
*
*  SCSIxThrottleTransfer
*
*     Throttle transfer speed and width for domain validation retry 
*
*  Return Value:  0 - transfer can be throttled down
*                 -1 - Can't throttle transfer anymore
*                  
*  Parameters:    adapterTSH
*                 targetTSCB
*
*  Remarks:       There are six possible stages in the process of
*                 throttling which is hold by SCSI_AF_dvThrottle
*                 under adapterTSH
*                 1. SCSI_DE_WIDE - DE/wide xfer (initial stage)
*                 2. SCSI_SE_WIDE - SE/wide xfer (initial stage)
*                 3. SCSI_SE_NARROW - SE/narrow xfer 
*                 4. SCSI_SE_WIDE_REPEAT - SE/wide xfer with lower
*                    speed (repeat to async)
*                 5. SCSI_SE_NARROW_REPEAT - SE/narrow xfer with
*                    lower speed (repeast to async) (initial stage)
*                 6. SCSI_ASYNC_NARROW - async/narrow xfer
*
*********************************************************************/
#if SCSI_DOMAIN_VALIDATION
HIM_UINT32 SCSIxThrottleTransfer (SCSI_ADAPTER_TSCB HIM_PTR adapterTSH,
                                  SCSI_TARGET_TSCB HIM_PTR targetTSCB)
{
   static SCSI_UEXACT8 se_rate[] ={ 0x0A, 0x0A, 0x0C, 0x0C, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19,
                  0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
                  0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0x32,
                  0x32, 0x32, 0x32, 0x32, 0x32, 0x32, 0xFF, };
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   SCSI_UEXACT8 index;
   SCSI_UEXACT8 scsiPeriod, scsiOffset, scsiPtclOpt, scsiMiscOpt;
   SCSI_DEVICE HIM_PTR deviceTable = &SCSI_DEVICE_TABLE(hhcb)[targetTSCB->scsiID];
   
   /* get current transfer rate and offset */
   scsiPeriod = SCSI_DV_XFER(deviceTable)[SCSI_XFER_PERIOD];
   scsiOffset = SCSI_DV_XFER(deviceTable)[SCSI_XFER_OFFSET];
   scsiPtclOpt = SCSI_DV_XFER(deviceTable)[SCSI_XFER_PTCL_OPT];
   scsiMiscOpt = SCSI_DV_XFER(deviceTable)[SCSI_XFER_MISC_OPT];

   /* set index for throttle speed */
   index = scsiPeriod - 8;
      
   if (scsiPeriod <= 0x08)
   {
      /* Throttle down from 320 Paced to 160 DE */
      SCSI_DV_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= 
           ~(SCSI_PCOMP_EN | SCSI_RTI | SCSI_HOLD_MCS);
#if SCSI_OEM1_SUPPORT
      /* A temporary solution to resolve the issue with the OEM1 U160 JBOD */
      /* box that might not able to handle packetized and QAS.              */
      /* Therefore, if it is the OEM1 hardware, CHIM will throttle the xfer */
      /* rate down to U160 only.  There should be no packetized, QAS and    */
      /* data streaming enable for this speed.                              */
      if (hhcb->OEMHardware == SCSI_OEMHARDWARE_OEM1)
      {
         SCSI_DV_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= 
                  ~(SCSI_RD_STRM | SCSI_WR_FLOW | SCSI_QAS_REQ | SCSI_IU_REQ);
         /*If throttle to 160 then no QAS for anyone for OEM1*/
         hhcb->SCSI_HF_adapterQASDisable=1;
      }
#endif /* SCSI_OEM1_SUPPORT */


      SCSI_DV_XFER(deviceTable)[SCSI_XFER_PERIOD] = SCSI_U320TC_160MBRATE;
   }
   else if (scsiPtclOpt & SCSI_DUALEDGE)
   {
      if (scsiPeriod == SCSI_U320TC_160MBRATE)
      {
#if SCSI_PACKETIZED_IO_SUPPORT
         /*If packetized try 160 without packetize else goto 80*/
         if (!(SCSI_DV_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] & SCSI_PACKETIZED))
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
         {
            /* Throttle down to 80MHz SE */
            SCSI_DV_XFER(deviceTable)[SCSI_XFER_PERIOD] = SCSI_U320TC_80MBRATE;
            SCSI_DV_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= ~(SCSI_DUALEDGE+SCSI_PACKETIZED);
            adapterTSH->SCSI_AF_dvThrottle(targetTSCB->scsiID) = SCSI_SE_WIDE;
            /* Macro SCSI_hPPRNEGONEEDED will cause PPR message if bits are set
             * in LAST_NEGO_ENTRY. Therefore we must clear this to ensure
             * Wide message.
             */
            SCSI_LAST_NEGO_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] = 0;
         }   
            
         SCSI_DV_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= ~(SCSI_PACKETIZED);
      }
      else if ((scsiPeriod == 0x0A) || (scsiPeriod == 0x0B))
      {
         /* Throttle down from 80/40MHz/26.67MHz DE to 40MHz SE */
         SCSI_DV_XFER(deviceTable)[SCSI_XFER_PERIOD] = 0x0A;
         SCSI_DV_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= ~(SCSI_DUALEDGE+SCSI_PACKETIZED);
         adapterTSH->SCSI_AF_dvThrottle(targetTSCB->scsiID) = SCSI_SE_WIDE;
      }
      else if ((0x0C <= scsiPeriod) && (scsiPeriod <= 0x18))
      {
         /* Throttle down from 20/16/13.33MHz DE to 20MHz SE */
         SCSI_DV_XFER(deviceTable)[SCSI_XFER_PERIOD] = 0x0C;
         SCSI_DV_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= ~(SCSI_DUALEDGE+SCSI_PACKETIZED);
         adapterTSH->SCSI_AF_dvThrottle(targetTSCB->scsiID) = SCSI_SE_WIDE;
      }
      else if ((0x19 <= scsiPeriod) && (scsiPeriod <= 0x31))
      {
         /* Throttle down from 10/8/6.7/5.7MHz DE to 10MHz SE */
         SCSI_DV_XFER(deviceTable)[SCSI_XFER_PERIOD] = 0x19;
         SCSI_DV_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] &= ~(SCSI_DUALEDGE+SCSI_PACKETIZED);
         adapterTSH->SCSI_AF_dvThrottle(targetTSCB->scsiID) = SCSI_SE_WIDE;
      }
   }
   else
   {
      if (scsiMiscOpt & SCSI_WIDE)
      {
         /* it's wide transfer */
         if (adapterTSH->SCSI_AF_dvThrottle(targetTSCB->scsiID) == SCSI_SE_WIDE ||
            adapterTSH->SCSI_AF_dvThrottle(targetTSCB->scsiID) == SCSI_DE_WIDE)
         {
            /* it's SE/wide, just make it SE/narrow */
            SCSI_DV_XFER(deviceTable)[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
            adapterTSH->SCSI_AF_dvThrottle(targetTSCB->scsiID) = SCSI_SE_NARROW;
         }
         else
         {
            /* it's must be SE/wide repeat, just lower transfer rate */
            if (scsiOffset != 0)
            {
               /* lower the transfer rate */
               SCSI_DV_XFER(deviceTable)[SCSI_XFER_MISC_OPT] |= SCSI_WIDE;
               SCSI_DV_XFER(deviceTable)[SCSI_XFER_PERIOD] = se_rate[index];
            }
            else
            {
               /* can't lower transfer rate anymore, make it narrow instead */
               SCSI_DV_XFER(deviceTable)[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
               adapterTSH->SCSI_AF_dvThrottle(targetTSCB->scsiID) = SCSI_ASYNC_NARROW;
            }
         }
      }
      else
      {
         /* it's narrow transfer */
         if (scsiOffset == 0)
         {
            /* it's async/narrow we can't throttle anymore */
            return((HIM_UINT32)-1);
         }
         else
         {
            if (adapterTSH->SCSI_AF_dvThrottle(targetTSCB->scsiID) == SCSI_SE_NARROW)
            {
               /* it's SE/narrow, we will restore wide with lower transfer rate */
               SCSI_DV_XFER(deviceTable)[SCSI_XFER_MISC_OPT] |= SCSI_WIDE;
               SCSI_DV_XFER(deviceTable)[SCSI_XFER_PERIOD] = se_rate[index];
               adapterTSH->SCSI_AF_dvThrottle(targetTSCB->scsiID) = SCSI_SE_WIDE_REPEAT;
            }
            else
            {
               /* it's must be SE/narrow repeat, we will just lower trasfer  */
               SCSI_DV_XFER(deviceTable)[SCSI_XFER_PERIOD] = se_rate[index];
            }
         }
      }
   }

   if (se_rate[index] == 0xFF)
   {
      /* assume async xfer */
      SCSI_DV_XFER(deviceTable)[SCSI_XFER_PERIOD] = SCSI_ASYNC_XFER_PERIOD;
      SCSI_DV_XFER(deviceTable)[SCSI_XFER_OFFSET] = 0;
      SCSI_DV_XFER(deviceTable)[SCSI_XFER_MISC_OPT] &= ~SCSI_WIDE;
   }

   /* Remember that we have fallen back from default xfer rate for this target*/
   adapterTSH->SCSI_AF_dvFallBack(targetTSCB->scsiID) = 1;
   
   return(0);
}
#endif /* SCSI_DOMAIN_VALIDATION */

/*********************************************************************
*
*  SCSIxQueueSpecialIOB
*
*     Queue request for special function operation
*
*  Return Value:  none
*                  
*  Parameters:    adapterTSH
*                 iob special function type
*
*  Remarks:
*
*********************************************************************/
void SCSIxQueueSpecialIOB (SCSI_ADAPTER_TSCB HIM_PTR adapterTSH,
                           SCSI_UINT8 iobFunctionType)
{
   SCSI_TARGET_TSCB HIM_PTR targetTSCB = &adapterTSH->targetTSCBBusScan;
   HIM_IOB HIM_PTR iob;

   /* setup special iob request (unfreezeinquiry) for bus scan */
   /* get iob memory */
   iob = (HIM_IOB HIM_PTR) (((SCSI_UEXACT8 HIM_PTR)adapterTSH->moreUnlocked)+SCSI_MORE_SPECIAL_IOB);
   OSDmemset(iob,0,sizeof(HIM_IOB));

   /* setup iob reserved memory */
   iob->iobReserve.virtualAddress = (void HIM_PTR) (((SCSI_UEXACT8 HIM_PTR)
               adapterTSH->moreLocked) + SCSI_MORE_IOBRSV);
   iob->iobReserve.bufferSize = sizeof(SCSI_IOB_RESERVE);
   iob->iobReserve.busAddress = OSMxGetBusAddress(SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                  HIM_MC_LOCKED,iob->iobReserve.virtualAddress);
   OSDmemset(iob->iobReserve.virtualAddress,0,sizeof(SCSI_IOB_RESERVE));

   /* setup/validate temporary target task set handle */
   SCSI_SET_UNIT_HANDLE(&SCSI_ADPTSH(adapterTSH)->hhcb,
         &targetTSCB->unitControl,(SCSI_UEXACT8)targetTSCB->scsiID,
         (SCSI_UEXACT8)targetTSCB->lunID);

   /* setup iob for special function */
   iob->function = (HIM_UINT8) iobFunctionType;
   iob->taskSetHandle = (HIM_TASK_SET_HANDLE) targetTSCB;
   iob->postRoutine = (HIM_POST_PTR) SCSIxPostQueueSpecialIOB;

   /* execute it */
   SCSIQueueIOB(iob);
}

/*********************************************************************
*
*  SCSIxPostQueueSpecialIOB
*
*     Handling post completion for special function operation
*
*  Return Value:  status
*                  
*  Parameters:    iob
*
*  Remarks:
*
*********************************************************************/
HIM_UINT32 SCSIxPostQueueSpecialIOB (HIM_IOB HIM_PTR iob)
{
   switch(iob->taskStatus)
   {
      case HIM_IOB_GOOD:
         break;

      default:
         return(HIM_FAILURE);
   }

   return(HIM_SUCCESS); 
}

/*********************************************************************
*
*  SCSIxDetermineFirmwareMode
*
*     Determines which firmware mode.
*
*  Return Value:  Firmware Mode
*
*  Parameters:    adapterTSH - Adapter Task Set Handle
*
*  Remarks:       None.
*
*********************************************************************/
SCSI_UEXACT8 SCSIxDetermineFirmwareMode (SCSI_ADAPTER_TSCB HIM_PTR adapterTSH)
{
   SCSI_UEXACT8 fwMode = SCSI_FMODE_NOT_ASSIGNED;
   SCSI_UEXACT8 hwType;
#if SCSI_STANDARD_ENHANCED_U320_MODE 
   SCSI_UEXACT8 devRevId;
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

   /* determine hardware type */
#if SCSI_DCH_U320_MODE
      hwType = 2;      /* DCH hardware type */
#else
   if (SCSI_IS_U320_ID(adapterTSH->productID))
   {
      hwType = 0;      /* harpoon (u320) hardware type */
#if SCSI_STANDARD_ENHANCED_U320_MODE
      /* Must be rev B ASIC to use this firmware mode. */ 
      devRevId = ((SCSI_UEXACT8)
                     OSMxReadPCIConfigurationDword(
                        SCSI_ADPTSH(adapterTSH)->osmAdapterContext,
                        SCSI_DEV_REV_ID_REG));

      if (SCSI_xHARPOON_REV_B(adapterTSH->productID,devRevId))
      {
         hwType = 1;
      }
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */
   }
   else
   {
      hwType = 3;      /* unknown hardware type */
   }
#endif /* SCSI_DCH_U320_MODE */

   /* setting of firmware mode */
   switch (hwType)
   {
      case 0:
#if SCSI_STANDARD_U320_MODE
         fwMode = SCSI_FMODE_STANDARD_U320;
#endif /* SCSI_STANDARD_U320_MODE */
         break;
      
      case 1:
#if SCSI_STANDARD_ENHANCED_U320_MODE
         fwMode = SCSI_FMODE_STANDARD_ENHANCED_U320;
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */
         break;

      case 2:
#if SCSI_DCH_U320_MODE
         fwMode = SCSI_FMODE_DCH_U320;
#endif /* SCSI_DCH_U320_MODE */
         break;

      case 3:
         fwMode = SCSI_FMODE_NOT_ASSIGNED;
         break;
   }

   return(fwMode);
}

/*********************************************************************
*
*  SCSIxU320GetNextSubSystemSubVendorID
*
*     This routine returns the next host device type not supported by
*     SCSI CHIM.
*
*  Return Value:  0 - No more device type
*                 host id
*                  
*  Parameters:    index
*                 mask
*
*  Remarks:       SCSI CHIM will make a subsystem/subvendor id table built
*                 at compile time. Parameter index will be used to
*                 index into that table. The table itself should be
*                 static at initialization/run time. The table is
*                 maintained at CHIM layer.
*
*********************************************************************/
HIM_HOST_ID SCSIxU320GetNextSubSystemSubVendorID (HIM_UINT16 index,
                                                  HIM_HOST_ID HIM_PTR mask)
{
   HIM_HOST_ID hostID = 0;
   if (index < (sizeof(SCSIU320SubSystemSubVendorType) / sizeof(SCSI_HOST_TYPE)))
   {
      hostID = SCSIU320SubSystemSubVendorType[index].idHost;
      *mask = SCSIU320SubSystemSubVendorType[index].idMask;
   }
   
   return(hostID);
}

#if SCSI_SCSISELECT_SUPPORT
/*********************************************************************
*
*  SCSIxDetermineValidNVData
*
*     This routine determines if the NV RAM is valid. 
*
*  Return Value:  0 - NV RAM check failed
*                 1 - NV RAM check successful
*
*  Parameters:    dataBuffer
*
*  Remarks:       
*                 
*********************************************************************/
SCSI_UEXACT8 SCSIxDetermineValidNVData (SCSI_NVM_LAYOUT HIM_PTR nvmdataptr)
{
   SCSI_UEXACT16 sum;
   SCSI_UEXACT8 seepromAllzero = 1;
   SCSI_UEXACT8 seepromAllff = 1;
   SCSI_UEXACT8 seepromdefaultchk = 1;
   SCSI_UINT i;
   SCSI_UEXACT16 HIM_PTR databuffer = (SCSI_UEXACT16 HIM_PTR) nvmdataptr;
      
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
   for (i = 0; i < OSMoffsetof(SCSI_NVM_LAYOUT,checkSum)/2; i++)
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
#endif /* SCSI_SCSISELECT_SUPPORT */

#if SCSI_TARGET_OPERATION
/*********************************************************************
*
*  OSDBuildTargetSCB
*
*     Build scb buffer
*
*  Return Value:  void
*                  
*  Parameters:    exercise
*
*  Remarks:       This routine is defined for internal HIM reference
*
*********************************************************************/
void OSDBuildTargetSCB (SCSI_HIOB SCSI_IPTR hiob)
{
   HIM_IOB HIM_PTR iob = SCSI_GETIOB(hiob);
   SCSI_NEXUS SCSI_XPTR nexusTSH = SCSI_NEXUS_UNIT(hiob);
   SCSI_HHCB SCSI_HPTR hhcb = nexusTSH->hhcb;
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH = SCSI_GETADP(hhcb);
  
   /* invoke the proper routine to build SCB */
   adapterTSH->OSDBuildTargetSCB(hiob,iob,nexusTSH,adapterTSH);
}

/*********************************************************************
*
*  OSDStandardU320BuildTargetSCB
*
*     Build SCB for target request
*
*  Return Value:  void
*                  
*  Parameters:    hiob
*
*  Remarks: This routine is used for establish connection scbs also.
*
*           To be implemented.  
*
*********************************************************************/ 
#if SCSI_STANDARD_U320_MODE
void OSDStandardU320BuildTargetSCB (SCSI_HIOB HIM_PTR hiob,
                                    HIM_IOB HIM_PTR iob,
                                    SCSI_NEXUS SCSI_XPTR nexusTSH,
                                    SCSI_ADAPTER_TSCB HIM_PTR adapterTSH)
{
   SCSI_IOB_RESERVE HIM_PTR iobReserve =
                  (SCSI_IOB_RESERVE HIM_PTR)iob->iobReserve.virtualAddress;
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   SCSI_SCB_STANDARD_U320 HIM_PTR scbBuffer =
         (SCSI_SCB_STANDARD_U320 HIM_PTR) hiob->scbDescriptor->scbBuffer.virtualAddress;
   SCSI_EST_SCB_STANDARD_U320 HIM_PTR estscbBuffer =
      (SCSI_EST_SCB_STANDARD_U320 HIM_PTR) hiob->scbDescriptor->scbBuffer.virtualAddress;
   SCSI_UEXACT32 sgData;
   SCSI_BUS_ADDRESS busAddress;
   SCSI_UEXACT8 sg_cache_SCB;
   SCSI_UEXACT8 scontrol;
   SCSI_UEXACT8 stypecode;     /* type of target mode SCB */
   SCSI_UEXACT8 scontrol1 = 0; 

   /* Set target mode flag in scontrol. */
   scontrol = SCSI_SU320_TARGETENB;

   /* build scb buffer */
   /* Use this routine to build establish connection SCBs also. */
   if (hiob->cmd == SCSI_CMD_ESTABLISH_CONNECTION)
   {
      /* Code to build establish connection SCB */

      /* Set stypecode to Est SCB (empty SCB). */
      stypecode = SCSI_SU320_EMPTYSCB;

      /* Host PCI address of this SCB for returning nexus information.
       * May change to a data buffer later.
       */
      /* PCI address of this SCB */
#if SCSI_SIMULATION_SUPPORT
     /* The only reason for a separate version of this portion of 
      * code is to reduce the size of the macro expansion. 
      * Previously, the simulation compiler would fault during
      * macro expansion.
      */
      busAddress = hiob->scbDescriptor->scbBuffer.busAddress;
      SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                          estscbBuffer,
                          OSMoffsetof(SCSI_EST_SCB_STANDARD_U320,
                                      scbaddress0),
                          busAddress);
#else
      SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                          estscbBuffer,
                          OSMoffsetof(SCSI_EST_SCB_STANDARD_U320,
                                      scbaddress0),
                          hiob->scbDescriptor->scbBuffer.busAddress);
#endif /* SCSI_SIMULATION_SUPPORT */
   }
   else
   {
      /* Code to build Reestablish SCB */ 

      /* Put our Host ID in the starget field. This is really for 
       * multiple ID support, and the sequencer requires it.
       */
#if SCSI_MULTIPLEID_SUPPORT
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_U320,starget),
                             nexusTSH->selectedID);  
#else
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_U320,starget),
                             hhcb->hostScsiID);  
#endif /* SCSI_MULTIPLEID_SUPPORT */
      
      /* SCSI ID of initiator to be reselected. */ 
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_U320,
                                         SCSI_SU320_sinitiator),
                             nexusTSH->scsiID);
      
      if (nexusTSH->SCSI_XF_tagRequest) 
      {
         /* Set tag enble bit in scontrol. */  
         scontrol |= SCSI_SU320_TAGENB;

         /* Tag number - only one byte for now. When packetized
          * need 16 bit.
          */        
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_U320,
                                            SCSI_SU320_starg_tagno),
                                nexusTSH->queueTag);  
      }

      /* Set lun field. */
      if (hhcb->SCSI_HF_targetHostTargetVersion == HIM_SCSI_2 &&
          !hhcb->SCSI_HF_targetScsi2RejectLuntar)
      {
         /* Need to include luntar bit in response */
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_U320,slun),
                                ((nexusTSH->lunID) |
                                 (nexusTSH->SCSI_XF_lunTar << 5)));  
      }
      else
      {
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_U320,slun),
                                nexusTSH->lunID);  
      }

      if (iob->function == HIM_REESTABLISH_AND_COMPLETE)
      { 
         /* For legacy no need to differentiate between good and
          * bad status.
          */
         stypecode = SCSI_SU320_GOODSTATUS;

         /* put in SCSI status ( assume 1 byte) */ 
         /* if (iob->targetCommandLength > 1)
            {
               HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,scbBuffer,
                  OSMoffsetof(SCSI_SCB_SWAPPING_160M,SCSI_WADV_starg_status),iob->targetCommand,1);    
            }
            else
            { 
               HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,scbBuffer,
                  OSMoffsetof(SCSI_SCB_SWAPPING_160M,SCSI_WADV_starg_status),iob->targetCommand,iob->targetCommandLength);    
            }
          */
         HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_U320,
                                         SCSI_SU320_starg_status),
                             iob->targetCommand,
                             1);    
      }
      else if ( (!nexusTSH->SCSI_XF_disconnectAllowed) ||
                (nexusTSH->SCSI_XF_lastResource) ||
                (hiob->SCSI_IF_disallowDisconnect) )
      {
         /* Hold the SCSI bus when                       */
         /*    1) disconnect privilege is not granted    */
         /*    2) the last resource is being used        */
         /*    3) the OSM specified not to disconect     */ 
         scontrol |= SCSI_SU320_HOLDONBUS;
      }

      if (!iob->data.bufferSize)
      {
         /* If the data buffer is not set up then don't 
          * attempt to transfer data. */
         sg_cache_SCB = SCSI_SU320_NODATA;
      }
      else
      if (iob->flagsIob.outboundData)
      {
         /* Update stypecode */
         /* Note: stypecode represents the SCSI phase. */
         if (hiob->cmd == SCSI_CMD_REESTABLISH_AND_COMPLETE)
         {
            stypecode = SCSI_SU320_DATAIN_AND_STATUS;
         }
         else
         {
            stypecode = SCSI_SU320_DATAIN;
         }
         sg_cache_SCB = 0;
      }
      else
      if (iob->flagsIob.inboundData)
      {
         /* Update stypecode */
         /* Note: stypecode represents the SCSI phase. */
         if (hiob->cmd == SCSI_CMD_REESTABLISH_AND_COMPLETE)
         {
            stypecode = SCSI_SU320_DATAOUT_AND_STATUS;
         }
         else
         {
            stypecode = SCSI_SU320_DATAOUT;
         }
         sg_cache_SCB = 0;
      }
      else
      {
         sg_cache_SCB = SCSI_SU320_NODATA;
      }

      /* Data Transfer */
      if (!(sg_cache_SCB & SCSI_SU320_NODATA))
      {
         /* setup the embedded s/g segment (the first s/g segment) */
         HIM_GET_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,&sgData,iob->data.virtualAddress,
            sizeof(SCSI_BUS_ADDRESS));

         /* Special code to handle one segment with a zero length */ 
         if (sgData == (SCSI_UEXACT32)0x80000000)
         {
            /* indicate no data transfer */
            sg_cache_SCB |= SCSI_SU320_NODATA;
         }
         else
         {
            if (sgData & (SCSI_UEXACT32)0x80000000)
            {
               /* There is only one s/g element in s/g list */
               sg_cache_SCB |= SCSI_SU320_ONESGSEG;
            }
            else
            {
               /* more than one s/g segments and need to setup s/g pointer */
               SCSI_hPUTBUSADDRESS(adapterTSH->hhcb,scbBuffer,OSMoffsetof(
                  SCSI_SCB_STANDARD_U320,SCSI_SU320_sg_pointer_SCB0),
                  iob->data.busAddress);
            }

            /* setup embedded sg segment length */
            HIM_PUT_LITTLE_ENDIAN24(adapterTSH->osmAdapterContext,scbBuffer,OSMoffsetof(
               SCSI_SCB_STANDARD_U320,slength0),sgData);

            /* setup embedded sg segment address */
            SCSI_hGETBUSADDRESS(&adapterTSH->hhcb,&busAddress,
               iob->data.virtualAddress,0);
            SCSI_hPUTBUSADDRESS(adapterTSH->hhcb,scbBuffer,
               OSMoffsetof(SCSI_SCB_STANDARD_U320,saddress0),busAddress);
                                       
         } 
      }

      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,scbBuffer,
         OSMoffsetof(SCSI_SCB_STANDARD_U320,sg_cache_SCB),
            (sg_cache_SCB | (2 * sizeof(SCSI_BUS_ADDRESS))));
   } 

   /* Set fields that are common to both SCB types - use the same
    * SCB location.
    */

   /* Set scontrol field. */  
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_U320,scontrol),
                          scontrol);

   /* Set stypecode field. */  
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_EST_SCB_STANDARD_U320,stypecode),
                          stypecode);

   /* Set array_site field. */
   HIM_PUT_LITTLE_ENDIAN16(adapterTSH->osmAdapterContext,
                           scbBuffer,
                           OSMoffsetof(SCSI_SCB_STANDARD_U320,
                                       SCSI_SU320_array_site),
                           hiob->scbDescriptor->scbNumber);

   /* Set next_SCB_addr field. */
#if SCSI_SIMULATION_SUPPORT
   /* The only reason for a separate version of this portion of 
    * code is to reduce the size of the macro expansion. 
    * Previously, the simulation compiler would fault during
    * macro expansion.
    */
   busAddress = hiob->scbDescriptor->queueNext->scbBuffer.busAddress;
   SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                       scbBuffer,
                       OSMoffsetof(SCSI_SCB_STANDARD_U320,
                                   SCSI_SU320_next_SCB_addr0),
                       busAddress);
#else
   SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                       scbBuffer,
                       OSMoffsetof(SCSI_SCB_STANDARD_U320,
                                   SCSI_SU320_next_SCB_addr0),
                       hiob->scbDescriptor->queueNext->scbBuffer.busAddress);
#endif /* SCSI_SIMULATION_SUPPORT */

   /* Sequencer spec requires that SCB_flags be initialized to 0. */
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_U320,
                                      SCB_flags),
                          (SCSI_UEXACT8) 0);

#if SCSI_RAID1
   /* NULL the mirror_SCB, to prevent the mirroring of the command by sequencer */
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_U320,mirror_SCB)+1,
                          SCSI_NULL_ENTRY);
#endif /* SCSI_RAID1 */

#if SCSI_SELTO_PER_IOB
   /* Not implemented yet */
   if (hiob->seltoPeriod > 0)
   {                                                                        
      /* The following is based on the assumption that ALTSTIM is 
       * always set to '0' in the normal path. The value needed to
       * set STIM and ALTSTIM to the desired value via an XOR is
       * hence: (CURRENT xor DESIRED). We map the requested value
       * in milliseconds to the nearest value supported by the
       * hardware.
       */
      scontrol1 = (SCSI_UEXACT8) (hhcb->SCSI_HF_selTimeout << 3);
      
      if (hiob->seltoPeriod == 1)
      {
         scontrol1 ^= (SCSI_W160M_ALTSTIM | 0x18);
      }
      else if (hiob->seltoPeriod == 2)
      {
         scontrol1 ^= (SCSI_W160M_ALTSTIM | 0x10);
      }
      else if (hiob->seltoPeriod <= 4)
      {
         scontrol1 ^= (SCSI_W160M_ALTSTIM | 0x08);
      }
      else if (hiob->seltoPeriod <= 32)
      {
         scontrol1 ^= 0x18;
      }
      else if (hiob->seltoPeriod <= 64)
      {
         scontrol1 ^= 0x10;
      }
      else if (hiob->seltoPeriod <= 128)
      {
         scontrol1 ^= 0x08;
      }

      scontrol1 |= SCSI_U320_SELTOPERIOB_ENABLE;
   }
#endif /* SCSI_SELTO_PER_IOB */

   /* Set the scontrol1 field. */
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_U320,scontrol1),
                          scontrol1);

   /* flush the cache ready for dma */
   SCSI_FLUSH_CACHE(scbBuffer,sizeof (SCSI_SCB_STANDARD_U320));

}
#endif /* SCSI_STANDARD_U320_MODE */

/*********************************************************************
*
*  OSDStandardEnhU320BuildTargetSCB
*
*     Build SCB for target request
*
*  Return Value:  void
*                  
*  Parameters:    hiob
*
*  Remarks: This routine is used for establish connection scbs also.
*
*           To be implemented.  
*
*********************************************************************/ 
#if SCSI_STANDARD_ENHANCED_U320_MODE
void OSDStandardEnhU320BuildTargetSCB (SCSI_HIOB HIM_PTR hiob,
                                       HIM_IOB HIM_PTR iob,
                                       SCSI_NEXUS SCSI_XPTR nexusTSH,
                                       SCSI_ADAPTER_TSCB HIM_PTR adapterTSH)
{
   SCSI_IOB_RESERVE HIM_PTR iobReserve =
                  (SCSI_IOB_RESERVE HIM_PTR)iob->iobReserve.virtualAddress;
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   SCSI_SCB_STANDARD_ENH_U320 HIM_PTR scbBuffer =
         (SCSI_SCB_STANDARD_ENH_U320 HIM_PTR) hiob->scbDescriptor->scbBuffer.virtualAddress;
   SCSI_EST_SCB_STANDARD_ENH_U320 HIM_PTR estscbBuffer =
      (SCSI_EST_SCB_STANDARD_ENH_U320 HIM_PTR) hiob->scbDescriptor->scbBuffer.virtualAddress;
   SCSI_UEXACT32 sgData;
   SCSI_BUS_ADDRESS busAddress;
   SCSI_UEXACT8 sg_cache_SCB;
   SCSI_UEXACT8 scontrol;
   SCSI_UEXACT8 stypecode;     /* type of target mode SCB */
   SCSI_UEXACT8 scontrol1 = 0; 
   SCSI_UEXACT8 scbFlags = 0;
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
   SCSI_DEVICE SCSI_DPTR deviceTable;
   SCSI_UEXACT8 packetized; /* set to 1 if packetized xfer rate negotiated */
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */

   /* Set target mode flag in scontrol. */
   scontrol = SCSI_SEU320_TARGETENB;

   /* build scb buffer */
   /* Use this routine to build establish connection SCBs also. */
   if (hiob->cmd == SCSI_CMD_ESTABLISH_CONNECTION)
   {
      /* Code to build establish connection SCB */

      /* Set stypecode to Est SCB (empty SCB). */
      stypecode = SCSI_SEU320_EMPTYSCB;

#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
      /* Set SCB_Flags to 1 for packetized establish SCBs */
      scbFlags = 1;

      /* Set SG_Cache_SCB to 2 - indicating last segment for
       * packetized establish connection SCBs.
       */
      sg_cache_SCB = 2;

      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,scbBuffer,
         OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,sg_cache_SCB),
            (sg_cache_SCB | (2 * sizeof(SCSI_BUS_ADDRESS))));
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */

      /* Host PCI address of this SCB for returning nexus information.
       * May change to a data buffer later.
       */
      /* PCI address of this SCB */
#if SCSI_SIMULATION_SUPPORT
      busAddress = hiob->scbDescriptor->scbBuffer.busAddress;
      SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                          estscbBuffer,
                          OSMoffsetof(SCSI_EST_SCB_STANDARD_ENH_U320,
                                      scbaddress0),
                          busAddress);
#else
      SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                          estscbBuffer,
                          OSMoffsetof(SCSI_EST_SCB_STANDARD_ENH_U320,
                                      scbaddress0),
                          hiob->scbDescriptor->scbBuffer.busAddress);
#endif /* SCSI_SIMULATION_SUPPORT */
   }
   else
   {
      /* Code to build Reestablish SCB */ 

#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
      /* Determine if negotiated for packetized */
      deviceTable = SCSI_GET_NODE(SCSI_NEXUS_UNIT(hiob))->deviceTable;
      packetized = (SCSI_CURRENT_XFER(deviceTable)[SCSI_XFER_PTCL_OPT] & SCSI_PACKETIZED);
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */

      /* Put our Host ID in the starget field. This is really for 
       * multiple ID support, and the sequencer requires it.
       */
#if SCSI_MULTIPLEID_SUPPORT
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,starget),
                             nexusTSH->selectedID);  
#else
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,starget),
                             hhcb->hostScsiID);  
#endif /* SCSI_MULTIPLEID_SUPPORT */
      
      /* SCSI ID of initiator to be reselected. */ 
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                         SCSI_SEU320_sinitiator),
                             nexusTSH->scsiID);
      
      if (nexusTSH->SCSI_XF_tagRequest) 
      {
         /* Set tag enble bit in scontrol. */  
         scontrol |= SCSI_SEU320_TAGENB;

         /* Tag number. */        
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
         /* Two byte tag */
         HIM_PUT_LITTLE_ENDIAN16(adapterTSH->osmAdapterContext,
                                 scbBuffer,
                                 OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                             SCSI_SEU320_starg_tagno),
                                 nexusTSH->queueTag);
#else
         /* One byte tag */
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                            SCSI_SEU320_starg_tagno),
                                nexusTSH->queueTag);
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
      }

      /* Set lun field. */
      if (hhcb->SCSI_HF_targetHostTargetVersion == HIM_SCSI_2 &&
          !hhcb->SCSI_HF_targetScsi2RejectLuntar)
      {
         /* Need to include luntar bit in response */
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,slun),
                                ((nexusTSH->lunID) |
                                 (nexusTSH->SCSI_XF_lunTar << 5)));  
      }
      else
      {
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,slun),
                                nexusTSH->lunID);  
      }

      if (iob->function == HIM_REESTABLISH_AND_COMPLETE)
      { 
         /* For legacy no need to differentiate between good and
          * bad status.
          */
         stypecode = SCSI_SEU320_GOODSTATUS;

         /* put in SCSI status ( assume 1 byte) */ 
         /* if (iob->targetCommandLength > 1)
            {
               HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,scbBuffer,
                  OSMoffsetof(SCSI_SCB_SWAPPING_160M,SCSI_WADV_starg_status),iob->targetCommand,1);    
            }
            else
            { 
               HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,scbBuffer,
                  OSMoffsetof(SCSI_SCB_SWAPPING_160M,SCSI_WADV_starg_status),iob->targetCommand,iob->targetCommandLength);    
            }
          */
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
         if (!(packetized))
         {   
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
            HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                            SCSI_SEU320_starg_status),
                                iob->targetCommand,
                                1);    
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
         }
         else
         {
            /* Need to set the DL_count field of the SCB */
            /* Actually don't believe I need this. */
            HIM_PUT_LITTLE_ENDIAN24(adapterTSH->osmAdapterContext,
                                    scbBuffer,
                                    OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                                SCSI_SEU320_dlcount0),
                                    (SCSI_UEXACT32)0);
         }
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
      }
      else if ( (!nexusTSH->SCSI_XF_disconnectAllowed) ||
                (nexusTSH->SCSI_XF_lastResource) ||
                (hiob->SCSI_IF_disallowDisconnect) )
      {
         /* Hold the SCSI bus when                       */
         /*    1) disconnect privilege is not granted    */
         /*    2) the last resource is being used        */
         /*    3) the OSM specified not to disconect     */ 
         scontrol |= SCSI_SEU320_HOLDONBUS;
      }

      if (!iob->data.bufferSize)
      {
         /* If the data buffer is not set up then don't 
          * attempt to transfer data. */
         sg_cache_SCB = SCSI_SEU320_NODATA;
      }
      else
      if (iob->flagsIob.outboundData)
      {
         /* Update stypecode */
         /* Note: stypecode represents the SCSI phase. */
         if (hiob->cmd == SCSI_CMD_REESTABLISH_AND_COMPLETE)
         {
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
            if ((((HIM_UEXACT8 HIM_PTR)iob->targetCommand)[0] !=
                (HIM_UEXACT8)SCSI_UNIT_GOOD) && (packetized))
            {
               /* OSM sending bad status */ 
               stypecode = SCSI_SEU320_BADSTATUS;
            }
            else
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
            {
               stypecode = SCSI_SEU320_DATAIN_AND_STATUS;
            }
         }
         else
         {
            stypecode = SCSI_SEU320_DATAIN;
         }
         sg_cache_SCB = 0;
      }
      else
      if (iob->flagsIob.inboundData)
      {
         /* Update stypecode */
         /* Note: stypecode represents the SCSI phase. */
         if (hiob->cmd == SCSI_CMD_REESTABLISH_AND_COMPLETE)
         {
            stypecode = SCSI_SEU320_DATAOUT_AND_STATUS;
         }
         else
         {
            stypecode = SCSI_SEU320_DATAOUT;
         }
         sg_cache_SCB = 0;
      }
      else
      {
         sg_cache_SCB = SCSI_SEU320_NODATA;
      }

      /* Data Transfer */
      if (!(sg_cache_SCB & SCSI_SEU320_NODATA))
      {
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
         if (packetized)
         {
            /* Need to set the DL_count field of the SCB */
            HIM_PUT_LITTLE_ENDIAN24(adapterTSH->osmAdapterContext,scbBuffer,OSMoffsetof(
               SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_dlcount0),iob->ioLength);
         }
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
         /* setup the embedded s/g segment (the first s/g segment) */
         HIM_GET_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,&sgData,iob->data.virtualAddress,
            sizeof(SCSI_BUS_ADDRESS));

         /* Special code to handle one segment with a zero length */ 
         if (sgData == (SCSI_UEXACT32)0x80000000)
         {
            /* indicate no data transfer */
            sg_cache_SCB |= SCSI_SEU320_NODATA;
         }
         else
         {
            if (sgData & (SCSI_UEXACT32)0x80000000)
            {
               /* There is only one s/g element in s/g list */
               sg_cache_SCB |= SCSI_SEU320_ONESGSEG;
            }
            else
            {
               /* more than one s/g segments and need to setup s/g pointer */
               SCSI_hPUTBUSADDRESS(adapterTSH->hhcb,scbBuffer,OSMoffsetof(
                  SCSI_SCB_STANDARD_ENH_U320,SCSI_SEU320_sg_pointer_SCB0),
                  iob->data.busAddress);
            }

            /* setup embedded sg segment length */
            HIM_PUT_LITTLE_ENDIAN24(adapterTSH->osmAdapterContext,scbBuffer,OSMoffsetof(
               SCSI_SCB_STANDARD_ENH_U320,slength0),sgData);

            /* setup embedded sg segment address */
            SCSI_hGETBUSADDRESS(&adapterTSH->hhcb,&busAddress,
               iob->data.virtualAddress,0);
            SCSI_hPUTBUSADDRESS(adapterTSH->hhcb,scbBuffer,
               OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,saddress0),busAddress);
                                       
         } 
      }

      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,scbBuffer,
         OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,sg_cache_SCB),
            (sg_cache_SCB | (2 * sizeof(SCSI_BUS_ADDRESS))));
   } 

   /* Set fields that are common to both SCB types - use the same
    * SCB location.
    */

   /* Set scontrol field. */  
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,scontrol),
                          scontrol);

   /* Set stypecode field. */  
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_EST_SCB_STANDARD_ENH_U320,stypecode),
                          stypecode);

   /* Set array_site field. */
   HIM_PUT_LITTLE_ENDIAN16(adapterTSH->osmAdapterContext,
                           scbBuffer,
                           OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                       SCSI_SEU320_array_site),
                           hiob->scbDescriptor->scbNumber);

   /* Set next_SCB_addr field. */
#if SCSI_SIMULATION_SUPPORT
   busAddress = hiob->scbDescriptor->queueNext->scbBuffer.busAddress;
   SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                       scbBuffer,
                       OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                   SCSI_SEU320_next_SCB_addr0),
                       busAddress);
#else
   SCSI_hPUTBUSADDRESS(&adapterTSH->hhcb,
                       scbBuffer,
                       OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                   SCSI_SEU320_next_SCB_addr0),
                       hiob->scbDescriptor->queueNext->scbBuffer.busAddress);
#endif /* SCSI_SIMULATION_SUPPORT */

   /* Sequencer spec requires that SCB_flags be initialized. */
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,
                                      SCB_flags),
                          scbFlags);

   /* Zero the task_management field in case this is a packetized
    * request.
    */
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,task_management),
                          (SCSI_UEXACT8) 0);

#if SCSI_RAID1
   /* NULL the mirror_SCB, to prevent the mirroring of the command by sequencer */
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,mirror_SCB)+1,
                          SCSI_NULL_ENTRY);
#endif /* SCSI_RAID1 */

#if SCSI_SELTO_PER_IOB
   /* Not implemented yet */
   if (hiob->seltoPeriod > 0)
   {                                                                        
      /* The following is based on the assumption that ALTSTIM is 
       * always set to '0' in the normal path. The value needed to
       * set STIM and ALTSTIM to the desired value via an XOR is
       * hence: (CURRENT xor DESIRED). We map the requested value
       * in milliseconds to the nearest value supported by the
       * hardware.
       */
      scontrol1 = (SCSI_UEXACT8) (hhcb->SCSI_HF_selTimeout << 3);
      
      if (hiob->seltoPeriod == 1)
      {
         scontrol1 ^= (SCSI_W160M_ALTSTIM | 0x18);
      }
      else if (hiob->seltoPeriod == 2)
      {
         scontrol1 ^= (SCSI_W160M_ALTSTIM | 0x10);
      }
      else if (hiob->seltoPeriod <= 4)
      {
         scontrol1 ^= (SCSI_W160M_ALTSTIM | 0x08);
      }
      else if (hiob->seltoPeriod <= 32)
      {
         scontrol1 ^= 0x18;
      }
      else if (hiob->seltoPeriod <= 64)
      {
         scontrol1 ^= 0x10;
      }
      else if (hiob->seltoPeriod <= 128)
      {
         scontrol1 ^= 0x08;
      }

      scontrol1 |= SCSI_U320_SELTOPERIOB_ENABLE;
   }
#endif /* SCSI_SELTO_PER_IOB */

   /* Set the scontrol1 field. */
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_STANDARD_ENH_U320,scontrol1),
                          scontrol1);

   /* flush the cache ready for dma */
   SCSI_FLUSH_CACHE(scbBuffer,sizeof (SCSI_SCB_STANDARD_ENH_U320));

}
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

/*********************************************************************
*
*  OSDDchU320BuildTargetSCB
*
*     Build SCB for target request
*
*  Return Value:  void
*                  
*  Parameters:    hiob
*
*  Remarks: This routine is used for establish connection scbs also.
*
*           To be implemented.  
*
*********************************************************************/ 
#if SCSI_DCH_U320_MODE
void OSDDchU320BuildTargetSCB (SCSI_HIOB HIM_PTR hiob,
                               HIM_IOB HIM_PTR iob,
                               SCSI_NEXUS SCSI_XPTR nexusTSH,
                               SCSI_ADAPTER_TSCB HIM_PTR adapterTSH)
{
   SCSI_IOB_RESERVE HIM_PTR iobReserve =
                  (SCSI_IOB_RESERVE HIM_PTR)iob->iobReserve.virtualAddress;
   SCSI_HHCB HIM_PTR hhcb = &adapterTSH->hhcb;
   SCSI_SCB_DCH_U320 HIM_PTR scbBuffer =
         (SCSI_SCB_DCH_U320 HIM_PTR) hiob->scbDescriptor->scbBuffer.virtualAddress;
   SCSI_EST_SCB_DCH_U320 HIM_PTR estscbBuffer =
      (SCSI_EST_SCB_DCH_U320 HIM_PTR) hiob->scbDescriptor->scbBuffer.virtualAddress;
   SCSI_UEXACT32 sgData;
   SCSI_UEXACT32 sgCount;
   SCSI_BUS_ADDRESS busAddress;
   SCSI_UEXACT16 sg_cache_SCB;
   SCSI_UEXACT8 scontrol;
   SCSI_UEXACT8 stypecode;     /* type of target mode SCB */
   SCSI_UEXACT8 scontrol1 = 0; 
   SCSI_UEXACT8 sgListSize = (sizeof(SCSI_BUS_ADDRESS) * 2);
   SCSI_UEXACT32 offSet;

#if SCSI_SIMULATION_SUPPORT
   SCSI_UEXACT8 x;
#endif /* SCSI_SIMULATION_SUPPORT */

   /* Set target mode flag in scontrol. */
   scontrol = SCSI_DCHU320_TARGETENB;

   /* build scb buffer */
   /* Use this routine to build establish connection SCBs also. */
   if (hiob->cmd == SCSI_CMD_ESTABLISH_CONNECTION)
   {
      /* Code to build establish connection SCB */

      /* Set stypecode to Est SCB (empty SCB). */
      stypecode = SCSI_DCHU320_EMPTYSCB;

      /* Host PCI address of this SCB for returning nexus information.
       * May change to a data buffer later.
       */

      /* Internal memory address of this SCB */

      busAddress = hiob->scbDescriptor->scbBuffer.busAddress;

      offSet =  OSMoffsetof(SCSI_EST_SCB_DCH_U320,
                            scbaddress0);
  
      HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                             estscbBuffer,
                             offSet,
                             busAddress);
      /* Pad upper dword with zeros */                                
      HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                             estscbBuffer, 
                             offSet+4, 
                             0l);
   }
   else
   {
      /* Code to build Reestablish SCB */ 

      /* Put our Host ID in the starget field. This is really for 
       * multiple ID support, and the sequencer requires it.
       */
#if SCSI_MULTIPLEID_SUPPORT
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_DCH_U320,starget),
                             nexusTSH->selectedID);  
#else
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_DCH_U320,starget),
                             hhcb->hostScsiID);  
#endif /* SCSI_MULTIPLEID_SUPPORT */
      
      /* SCSI ID of initiator to be reselected. */ 
      HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_DCH_U320,
                                         SCSI_DCHU320_sinitiator),
                             nexusTSH->scsiID);
      
      if (nexusTSH->SCSI_XF_tagRequest) 
      {
         /* Set tag enble bit in scontrol. */  
         scontrol |= SCSI_DCHU320_TAGENB;

         /* Tag number - only one byte for now. When packetized
          * need 16 bit.
          */        
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_DCH_U320,
                                            SCSI_DCHU320_starg_tagno),
                                nexusTSH->queueTag);  
      }

      /* Set lun field. */
      if (hhcb->SCSI_HF_targetHostTargetVersion == HIM_SCSI_2 &&
          !hhcb->SCSI_HF_targetScsi2RejectLuntar)
      {
         /* Need to include luntar bit in response */
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_DCH_U320,slun),
                                ((nexusTSH->lunID) |
                                 (nexusTSH->SCSI_XF_lunTar << 5)));  
      }
      else
      {
         HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                                scbBuffer,
                                OSMoffsetof(SCSI_SCB_DCH_U320,slun),
                                nexusTSH->lunID);  
      }

      if (iob->function == HIM_REESTABLISH_AND_COMPLETE)
      { 
         /* For legacy no need to differentiate between good and
          * bad status.
          */
         stypecode = SCSI_DCHU320_GOODSTATUS;

         /* put in SCSI status ( assume 1 byte) */ 
         /* if (iob->targetCommandLength > 1)
            {
               HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,scbBuffer,
                  OSMoffsetof(SCSI_SCB_SWAPPING_160M,SCSI_WADV_starg_status),iob->targetCommand,1);    
            }
            else
            { 
               HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,scbBuffer,
                  OSMoffsetof(SCSI_SCB_SWAPPING_160M,SCSI_WADV_starg_status),iob->targetCommand,iob->targetCommandLength);    
            }
          */
         HIM_PUT_BYTE_STRING(adapterTSH->osmAdapterContext,
                             scbBuffer,
                             OSMoffsetof(SCSI_SCB_DCH_U320,
                                         SCSI_DCHU320_starg_status),
                             iob->targetCommand,
                             1);    
      }
      else if ( (!nexusTSH->SCSI_XF_disconnectAllowed) ||
                (nexusTSH->SCSI_XF_lastResource) ||
                (hiob->SCSI_IF_disallowDisconnect) )
      {
         /* Hold the SCSI bus when                       */
         /*    1) disconnect privilege is not granted    */
         /*    2) the last resource is being used        */
         /*    3) the OSM specified not to disconect     */ 
         scontrol |= SCSI_DCHU320_HOLDONBUS;
      }

      sg_cache_SCB = ((2 * sgListSize) << 8);
      if (!iob->data.bufferSize)
      {
         /* If the data buffer is not set up then don't 
          * attempt to transfer data. */
         sg_cache_SCB |= SCSI_DCHU320_NODATA;
      }
      else
      if (iob->flagsIob.outboundData)
      {
         /* Update stypecode */
         /* Note: stypecode represents the SCSI phase. */
         if (hiob->cmd == SCSI_CMD_REESTABLISH_AND_COMPLETE)
         {
            stypecode = SCSI_DCHU320_DATAIN_AND_STATUS;
         }
         else
         {
            stypecode = SCSI_DCHU320_DATAIN;
         }
         sg_cache_SCB &= 0xff00;    /* $$ DCH $$ */
      }
      else
      if (iob->flagsIob.inboundData)
      {
         /* Update stypecode */
         /* Note: stypecode represents the SCSI phase. */
         if (hiob->cmd == SCSI_CMD_REESTABLISH_AND_COMPLETE)
         {
            stypecode = SCSI_DCHU320_DATAOUT_AND_STATUS;
         }
         else
         {
            stypecode = SCSI_DCHU320_DATAOUT;
         }
         sg_cache_SCB &= 0xff00;    /* $$ DCH $$ */
      }
      else
      {
         sg_cache_SCB |= SCSI_DCHU320_NODATA;
      }

      /* Data Transfer */
      if (!(sg_cache_SCB & SCSI_DCHU320_NODATA))
      {
         /* setup the embedded s/g segment (the first s/g segment) */
         HIM_GET_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,
                                 &sgCount,
                                 iob->data.virtualAddress,
                                 sgListSize);

         /* setup the embedded s/g segment (the first s/g segment) */
         HIM_GET_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,
                                 &sgData,
                                 iob->data.virtualAddress,
                                 sgListSize + 4);

         /* Changes for DCH Common SG list (LE and LL support) .. $$ DCH $$ */

         /* Special code to handle one segment with a zero length */ 
         if ((sgCount == (SCSI_UEXACT32)0x00000000) &&
             (sgData & (SCSI_UEXACT32)0x40000000)) 
         {
            /* indicate no data transfer */
            sg_cache_SCB |= SCSI_DCHU320_NODATA;
         }
         else
         {
            /* add ASEL bits to sg_cache_SCB */
            sg_cache_SCB |= ((SCSI_UEXACT32)(sgData >> SCSI_DCHU320_3BYTE) 
                              & SCSI_DCHU320_ASEL_MASK);

            /* 1st s/g element should be done like this */
            /* setup the embedded s/g segment (the first s/g segment) */
            if (sgData & (SCSI_UEXACT32)0x40000000)
            {
               /* There is only one s/g element in s/g list */
               sg_cache_SCB |= SCSI_DCHU320_ONESGSEG;
            }
            else
            {
               /* more than one s/g segments, need to setup s/g pointer */

               busAddress = iob->data.busAddress;

               offSet =  OSMoffsetof(SCSI_SCB_DCH_U320,
                                     SCSI_DCHU320_sg_pointer_SCB0);
        
               HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                                      scbBuffer,
                                      offSet,
                                      busAddress);

               HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                                      scbBuffer, 
                                      offSet+4, 
                                      0l);
            }

            /* DCH uses 32 bit count field in SG list .. $$ DCH $$ */
            HIM_PUT_LITTLE_ENDIAN32(adapterTSH->osmAdapterContext,
                                    scbBuffer,
                                    OSMoffsetof(SCSI_SCB_DCH_U320,slength0),
                                    sgCount);

            /* write segment address */
            offSet =  OSMoffsetof(SCSI_SCB_DCH_U320,
                                  saddress0);
            /* get the 0-31 */
            SCSI_hGETBUSADDRESS(&adapterTSH->hhcb,
                                &busAddress,
                                iob->data.virtualAddress,
                                0);

            HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                                   scbBuffer,
                                   offSet,
                                   busAddress);

            /* get the 32-63 */
            SCSI_hGETBUSADDRESS(&adapterTSH->hhcb,
                                &busAddress,
                                iob->data.virtualAddress,
                                4);
        
            HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                                   scbBuffer, 
                                   offSet+4, 
                                   busAddress);
         } 
      }

      HIM_PUT_LITTLE_ENDIAN16(adapterTSH->osmAdapterContext,
                              scbBuffer,
                              OSMoffsetof(SCSI_SCB_DCH_U320,sg_cache_SCB),
                              sg_cache_SCB);
   } 

   /* Set fields that are common to both SCB types - use the same
    * SCB location.
    */

   /* Set scontrol field. */  
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,scontrol),
                          scontrol);

   /* Set stypecode field. */  
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_EST_SCB_DCH_U320,stypecode),
                          stypecode);

   /* Set array_site field. */
   HIM_PUT_LITTLE_ENDIAN16(adapterTSH->osmAdapterContext,
                           scbBuffer,
                           OSMoffsetof(SCSI_SCB_DCH_U320,
                                       SCSI_DCHU320_array_site),
                           hiob->scbDescriptor->scbNumber);

   /* Set next_SCB_addr field. */
   busAddress = hiob->scbDescriptor->queueNext->scbBuffer.busAddress;
   
   offSet =  OSMoffsetof(SCSI_SCB_DCH_U320,
                         SCSI_DCHU320_next_SCB_addr0);
   
   HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                          scbBuffer,
                          offSet,
                          busAddress);
                          
   HIM_PUT_LITTLE_ENDIAN32(&adapterTSH->hhcb,
                          scbBuffer, 
                          offSet+4, 
                          0l);

   /* Sequencer spec requires that SCB_flags be initialized to 0. */
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,
                                      SCB_flags),
                          (SCSI_UEXACT8) 0);

#if SCSI_RAID1
   /* NULL the mirror_SCB, to prevent the mirroring of the command by sequencer */
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,mirror_SCB)+1,
                          SCSI_NULL_ENTRY);
#endif /* SCSI_RAID1 */

#if SCSI_SELTO_PER_IOB
   /* Not implemented yet */
   if (hiob->seltoPeriod > 0)
   {                                                                        
      /* The following is based on the assumption that ALTSTIM is 
       * always set to '0' in the normal path. The value needed to
       * set STIM and ALTSTIM to the desired value via an XOR is
       * hence: (CURRENT xor DESIRED). We map the requested value
       * in milliseconds to the nearest value supported by the
       * hardware.
       */
      scontrol1 = (SCSI_UEXACT8) (hhcb->SCSI_HF_selTimeout << 3);
      
      if (hiob->seltoPeriod == 1)
      {
         scontrol1 ^= (SCSI_W160M_ALTSTIM | 0x18);
      }
      else if (hiob->seltoPeriod == 2)
      {
         scontrol1 ^= (SCSI_W160M_ALTSTIM | 0x10);
      }
      else if (hiob->seltoPeriod <= 4)
      {
         scontrol1 ^= (SCSI_W160M_ALTSTIM | 0x08);
      }
      else if (hiob->seltoPeriod <= 32)
      {
         scontrol1 ^= 0x18;
      }
      else if (hiob->seltoPeriod <= 64)
      {
         scontrol1 ^= 0x10;
      }
      else if (hiob->seltoPeriod <= 128)
      {
         scontrol1 ^= 0x08;
      }

      scontrol1 |= SCSI_U320_SELTOPERIOB_ENABLE;
   }
#endif /* SCSI_SELTO_PER_IOB */

   /* Set the scontrol1 field. */
   HIM_PUT_LITTLE_ENDIAN8(adapterTSH->osmAdapterContext,
                          scbBuffer,
                          OSMoffsetof(SCSI_SCB_DCH_U320,scontrol1),
                          scontrol1);

   /* flush the cache ready for dma */
   SCSI_FLUSH_CACHE(scbBuffer,sizeof (SCSI_SCB_DCH_U320));

}
#endif /* SCSI_DCH_U320_MODE */
/*********************************************************************
*
*  SCSIxTargetTaskManagementRequest
*
*     Target mode handling of a Task Management function
*
*  Return Value:  none 
*                  
*  Parameters:    iob
*                 hiob
*
*  Remarks:  This routine is invoked when a Task management function 
*            has been received from an initiator (indicated by an hiob 
*            stat value of SCSI_TASK_CMND_COMP).
*            The task management function (indicated by the contents of
*            the haStat field of the hiob) is translated.
*        
*********************************************************************/
void SCSIxTargetTaskManagementRequest (HIM_IOB HIM_PTR iob,
                                       SCSI_HIOB HIM_PTR hiob)
{
   SCSI_NEXUS SCSI_XPTR nexus = SCSI_NEXUS_UNIT(hiob);
   SCSI_HHCB SCSI_XPTR hhcb = nexus->hhcb;
   SCSI_UEXACT8 SCSI_SPTR cmndBuffer;
   
   /* Check if TMF can be stored in the buffer */
   if (iob->targetCommandBufferSize >= 1)
   {
      iob->targetCommandLength = 1;
      cmndBuffer = iob->targetCommand;
      
      /* interpret haStat of hiob */
      switch(hiob->haStat)
      {
         case SCSI_HOST_TARGET_RESET:
            /* SCSI-2 Bus device reset or SCSI-3 Target Reset */
            cmndBuffer[0] = HIM_TARGET_RESET;
            break;

         case SCSI_HOST_ABORT_TASK_SET:
            /* SCSI-2 Abort or SCSI-3 Abort Task Set */
            cmndBuffer[0] = HIM_ABORT_TASK_SET;
            break;

         case SCSI_HOST_ABORT_TASK:
            /* SCSI-2 Abort Tag or SCSI-3 AbortTask */
            cmndBuffer[0] = HIM_ABORT_TASK;
            break; 
             
         case SCSI_HOST_CLEAR_TASK_SET:
            /* SCSI-2 Clear Queue or SCSI-3 ClearTask Set */
            cmndBuffer[0] = HIM_CLEAR_TASK_SET;
            break;

         case SCSI_HOST_TERMINATE_TASK:
            /* SCSI-2 Terminate I/O process or SCSI-3 Terminate Task */
            cmndBuffer[0] = HIM_TERMINATE_TASK;
            break;

         case SCSI_HOST_CLEAR_ACA:
            /* SCSI-3 Clear ACA */
            cmndBuffer[0] = HIM_CLEAR_XCA;
            break;

         case SCSI_HOST_LUN_RESET:
            /* SCSI-3 Logical Unit Reset */  
            cmndBuffer[0] = HIM_LOGICAL_UNIT_RESET;
            break;
           
         default:
            /* should never come to here */
            break;
      }  

      if (iob->function == HIM_ESTABLISH_CONNECTION)
      {
         iob->flagsIob.targetRequestType = HIM_REQUEST_TYPE_TMF;
         iob->taskStatus = HIM_IOB_GOOD;
      }
      else
      {
         iob->taskStatus = HIM_IOB_ABORTED_TMF_RECVD;
      }
   }   
   else
   {
      /* No room in the command buffer */   
      iob->targetCommandLength = 0;
      iob->taskStatus = HIM_IOB_TARGETCOMMANDBUFFER_OVERRUN;
      if (iob->function == HIM_ESTABLISH_CONNECTION)
      {
         iob->flagsIob.targetRequestType = HIM_REQUEST_TYPE_TMF;
      }
   }
}

#if SCSI_MULTIPLEID_SUPPORT
/*********************************************************************
*
*  SCSIxEnableID
*
*     Validate HIM_ENABLE_ID IOB function
*
*  Return Value:  HIM_TRUE if HIM_ENABLE_ID passed validation
*                  
*  Parameters:    iob
*
*  Remarks:
*
*********************************************************************/
HIM_BOOLEAN SCSIxEnableID (HIM_IOB HIM_PTR iob)
{
   HIM_TASK_SET_HANDLE taskSetHandle = iob->taskSetHandle;
   SCSI_HHCB HIM_PTR hhcb = &SCSI_ADPTSH(taskSetHandle)->hhcb;
   HIM_TS_ID_SCSI HIM_PTR transportSpecific = iob->transportSpecific;
   SCSI_UINT8 count;
   SCSI_UINT8 i;

   if (transportSpecific)
   { 
      count = 0; /*  use this variable as a counter */
      /* 16 bit field */
      for (i = 0; i < SCSI_MAXDEV; i++)
      {
         /* count the bits */
         if (transportSpecific->scsiIDMask & (1 << i))
         {
            count++;
         }
         if (hhcb->SCSI_HF_targetAdapterIDMask & (1 << i))
         {
            count++;
         }
      }
   } 

   if (!transportSpecific ||
       count > hhcb->SCSI_HF_targetNumIDs ||
       !transportSpecific->scsiIDMask ||
       !hhcb->SCSI_HF_targetMode ||
       hhcb->SCSI_HF_initiatorMode)
   {
      return (HIM_FALSE);
   }

   return(HIM_TRUE);
}

/*********************************************************************
*
*  SCSIxDisableID
*
*     validate HIM_DISABLE_ID IOB function
*
*  Return Value:  HIM_TRUE if HIM_DISABLE_IOB passed validation
*                  
*  Parameters:    iob
*
*  Remarks:
*
*********************************************************************/
HIM_BOOLEAN SCSIxDisableID (HIM_IOB HIM_PTR iob)
{
   HIM_TASK_SET_HANDLE taskSetHandle = iob->taskSetHandle;
   SCSI_HHCB HIM_PTR hhcb = &SCSI_ADPTSH(taskSetHandle)->hhcb;
   HIM_TS_ID_SCSI HIM_PTR transportSpecific = iob->transportSpecific;

   if (!transportSpecific ||
       !hhcb->SCSI_HF_targetMode ||
       hhcb->SCSI_HF_initiatorMode)
   {
      return (HIM_FALSE);
   }

   return(HIM_TRUE);

}
#endif /* SCSI_MULTIPLEID_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */

#if SCSI_DOMAIN_VALIDATION
/*********************************************************************
*
*  SCSIxHandledvThrottleOSMReset
*
*     Handle Adapter profile field
*     AP_SCSIOSMResetBusUseThrottledDownSpeedforDV
*
*  Return Value:  void
*                  
*  Parameters:    hhcb
*
*  Remarks:       This routine determines a data hang condition
*                 and also checks if the profile
*                 AP_SCSIOSMResetBusUseThrottledDownSpeedforDV is
*                 enabled and if both true will throttle down the
*                 speed so the next domain validation will use the
*                 throttled down speed.
*                 
*********************************************************************/
void SCSIxHandledvThrottleOSMReset(SCSI_HHCB SCSI_HPTR hhcb)
{
   SCSI_HIOB HIM_PTR hiob;
   SCSI_ADAPTER_TSCB HIM_PTR adapterTSH; 
   SCSI_UEXACT16 scbNumber; 
   SCSI_TARGET_TSCB HIM_PTR targetTSCB;
   HIM_IOB HIM_PTR activeIob;
   SCSI_UEXACT8 dvState;

   if ((hhcb->SCSI_HF_dvThrottleOSMReset) && 
       (!SCSI_IS_NULL_SCB(scbNumber = SCSI_DATA_BUS_HANG(hhcb))))
   {
      if ((hiob = SCSI_ACTPTR[scbNumber]) != SCSI_NULL_HIOB)
      {
         activeIob = SCSI_GETIOB(hiob);
                     
         if (SCSI_TRGTSH(activeIob->taskSetHandle)->typeTSCB == SCSI_TSCB_TYPE_TARGET)
         {
            targetTSCB = (SCSI_TARGET_TSCB HIM_PTR) activeIob->taskSetHandle;
                                                
            adapterTSH = SCSI_GETADP(hhcb);
            dvState = (SCSI_UEXACT8)adapterTSH->SCSI_AF_dvState(targetTSCB->scsiID);

            if ((adapterTSH->SCSI_AF_dvLevel(targetTSCB->scsiID) != SCSI_DV_DISABLE) && 
               ((dvState == SCSI_DV_STATE_DONE) ||
               (dvState == SCSI_DV_STATE_INQ_OUTSTANDING) ||
               (dvState == SCSI_DV_STATE_REBD_OUTSTANDING) || 
               (dvState == SCSI_DV_STATE_WEB_OUTSTANDING) ||
               (dvState == SCSI_DV_STATE_REB_OUTSTANDING)) &&
               (!SCSIxThrottleTransfer(adapterTSH,targetTSCB)))
            {
               adapterTSH->SCSI_AF_dvThrottleOSMReset(targetTSCB->scsiID) = 1;
            }
         }
      }
   }
}
#endif /* SCSI_DOMAIN_VALIDATION */

#if SCSI_PACKETIZED_IO_SUPPORT           
#if (SCSI_TASK_SWITCH_SUPPORT == 2)
/*********************************************************************
*
*  OSDGetTargetCmd
*
*     Get first byte of CDB
*
*  Return Value:  SCSI_UEXACT8
*                  
*  Parameters:    hiob
*
*  Remarks:       This routine is defined for internal HIM reference.
*
*********************************************************************/
SCSI_UEXACT8 OSDGetTargetCmd (SCSI_HIOB SCSI_IPTR hiob)
{
   HIM_IOB HIM_PTR iob = SCSI_GETIOB(hiob);
   SCSI_UEXACT8 HIM_PTR targetCmd = (SCSI_UEXACT8 HIM_PTR) iob->targetCommand;

   return ((SCSI_UEXACT8) (targetCmd[0]));
}
#endif /* SCSI_TASK_SWITCH_SUPPORT == 2 */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */


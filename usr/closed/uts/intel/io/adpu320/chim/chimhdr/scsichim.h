/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * $Header: /vobs/u320chim/src/chim/chimhdr/scsichim.h
 * /main/17   Mon Mar 17 18:19:10 2003   quan $
 */

/*
 * Copyright 1995-2003 Adaptec, Inc.  All Rights Reserved.                  *
 *                                                                          *
 * This software contains the valuable trade secrets of Adaptec or its      *
 * licensors.  The software is protected under international copyright      *
 * laws and treaties.  This software may only be used in accordance with    *
 * terms of its accompanying license agreement.                             *
 */


/***************************************************************************
*
*  Module Name:   SCSICHIM.H
*
*  Description:   Shortcut front-end to include all other SCSI .h's.
*
*  Owners:        ECX IC Firmware Team
*
***************************************************************************/

#ifndef	_SCSICHIM_H
#define	_SCSICHIM_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%  Definitions originated from chimhw.h
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
#define  SCSI_HA_LITTLE_ENDIAN   HIM_HA_LITTLE_ENDIAN
#define  SCSI_SG_LIST_TYPE       HIM_SG_LIST_TYPE
#define  SCSI_HOST_BUS           HIM_HOST_BUS

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%  Definitions originated from chimcom.h implementation
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
typedef HIM_DOUBLET SCSI_DOUBLET;
typedef HIM_QUADLET SCSI_QUADLET;
typedef HIM_OCTLET SCSI_OCTLET;
typedef HIM_BUFFER_DESCRIPTOR SCSI_BUFFER_DESCRIPTOR;
typedef HIM_HOST_ID SCSI_HOST_ID;
typedef HIM_HOST_ADDRESS SCSI_HOST_ADDRESS;

#if SCSI_DCH_U320_MODE
#define SCSI_HOST_BUS_RBI     HIM_HOST_BUS_RBI     /* RBI bus            */
#endif /* SCSI_DCH_U320_MODE */

#define SCSI_HOST_BUS_PCI     HIM_HOST_BUS_PCI     /* PCI bus            */
#define SCSI_HOST_BUS_EISA    HIM_HOST_BUS_EISA    /* EISA bus           */
#define SCSI_MC_UNLOCKED      HIM_MC_UNLOCKED      /* unlocked memory    */
#define SCSI_MC_LOCKED        HIM_MC_LOCKED        /* locked memory      */

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%  Definitions originated from chimosm.h
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
#define OSD_CPU_LITTLE_ENDIAN    OSM_CPU_LITTLE_ENDIAN
#define OSD_DMA_SWAP_ENDIAN      OSM_DMA_SWAP_ENDIAN  
#define OSD_DMA_SWAP_WIDTH       OSM_DMA_SWAP_WIDTH       
#define OSD_BUS_ADDRESS_SIZE     OSM_BUS_ADDRESS_SIZE 
#define OSD_CPU_ADDRESS_SIZE     OSM_CPU_ADDRESS_SIZE

#define SCSI_HPTR       HIM_PTR 
#define SCSI_IPTR       HIM_PTR 
#define SCSI_FPTR       HIM_PTR 
#define SCSI_SPTR       HIM_PTR 
#define SCSI_LPTR       HIM_PTR
#define SCSI_UPTR       HIM_PTR
#define SCSI_DPTR       HIM_PTR
#define SCSI_XPTR       HIM_PTR     /* Nexus */
#define SCSI_NPTR       HIM_PTR     /* Node  */

#define SCSI_SINT8      HIM_SINT8
#define SCSI_UINT8      HIM_UINT8   
#define SCSI_SEXACT8    HIM_SEXACT8 
#define SCSI_UEXACT8    HIM_UEXACT8 
#define SCSI_SINT16     HIM_SINT16  
#define SCSI_UINT16     HIM_UINT16  
#define SCSI_SEXACT16   HIM_SEXACT16
#define SCSI_UEXACT16   HIM_UEXACT16
#define SCSI_SINT32     HIM_SINT32
#define SCSI_UINT32     HIM_UINT32  
#define SCSI_SEXACT32   HIM_SEXACT32
#define SCSI_UEXACT32   HIM_UEXACT32

typedef HIM_BUS_ADDRESS SCSI_BUS_ADDRESS;
typedef HIM_BOOLEAN     SCSI_BOOLEAN;
#define SCSI_FALSE      HIM_FALSE
#define SCSI_TRUE       HIM_TRUE

typedef struct 
{
   HIM_UINT8 memoryMapped;
      /* Allows OSM to choose:              */
      /*    HIM_IOSPACE - I/O mapped        */
      /*    HIM_MEMORYSPACE - memory mapped */
      /* HIM_MIXED_RANGES - not used        */
   HIM_IO_HANDLE ioHandle;
   HIM_IO_HANDLE ioHandleHigh;
   HIM_UEXACT8 (*OSMReadUExact8)(HIM_IO_HANDLE,HIM_UINT32);
   HIM_UEXACT16 (*OSMReadUExact16)(HIM_IO_HANDLE,HIM_UINT32);
   void (*OSMWriteUExact8)(HIM_IO_HANDLE,HIM_UINT32,HIM_UEXACT8);
   void (*OSMWriteUExact16)(HIM_IO_HANDLE,HIM_UINT32,HIM_UEXACT16);
   void (*OSMSynchronizeRange)(HIM_IO_HANDLE,HIM_UINT32,HIM_UINT32);
} SCSI_IO_HANDLE;


#define SCSI_FLUSH_CACHE      HIM_FLUSH_CACHE
#define SCSI_INVALIDATE_CACHE HIM_INVALIDATE_CACHE

#define OSDmemset(buf,value,len) OSMmemset((void HIM_PTR)(buf),(SCSI_UEXACT8)(value),(int)(len))
#define OSDmemcmp(buf1,buf2,len) OSMmemcmp((void HIM_PTR)(buf1),(void HIM_PTR)(buf2),(int)(len))
#define OSDmemcpy(buf1,buf2,len) OSMmemcpy((void HIM_PTR)(buf1),(void HIM_PTR)(buf2),(int)(len))

#define OSDDebugMode       OSMDebugMode
#define OSDScreenHandle    OSMScreenHandle

#define OSDoffsetof        OSMoffsetof
#define OSDAssert          OSMAssert
#define OSDDebugPrint      OSMDebugPrint
#define OSDEnterDebugger   OSMEnterDebugger


#define SCSI_PUT_BYTE_STRING(hhcb,dmaStruct,offset,pSrc,length)\
            HIM_PUT_BYTE_STRING((hhcb)->osdHandle,dmaStruct,offset,pSrc,length)
#define SCSI_PUT_LITTLE_ENDIAN8(hhcb,dmaStruct,offset,value)   \
            HIM_PUT_LITTLE_ENDIAN8((hhcb)->osdHandle,(dmaStruct),(offset),(value))
#define SCSI_PUT_LITTLE_ENDIAN16(hhcb,dmaStruct,offset,value)  \
            HIM_PUT_LITTLE_ENDIAN16((hhcb)->osdHandle,(dmaStruct),(offset),(value))
#define SCSI_PUT_LITTLE_ENDIAN24(hhcb,dmaStruct,offset,value)  \
             HIM_PUT_LITTLE_ENDIAN24((hhcb)->osdHandle,(dmaStruct),(offset),(value))
#define SCSI_PUT_LITTLE_ENDIAN32(hhcb,dmaStruct,offset,value)  \
            HIM_PUT_LITTLE_ENDIAN32((hhcb)->osdHandle,(dmaStruct),(offset),(value))
#define SCSI_GET_BYTE_STRING(hhcb,pDest,dmaStruct,offset,length)\
            HIM_GET_BYTE_STRING((hhcb)->osdHandle,pDest,dmaStruct,offset,length)
#define SCSI_PUT_LITTLE_ENDIAN64(hhcb,dmaStruct,offset,value)  \
            HIM_PUT_LITTLE_ENDIAN64((hhcb)->osdHandle,(dmaStruct),(offset),(value))
#define SCSI_GET_LITTLE_ENDIAN8(hhcb,pValue,dmaStruct,offset)   \
            HIM_GET_LITTLE_ENDIAN8((hhcb)->osdHandle,(pValue),(dmaStruct),(offset))
#define SCSI_GET_LITTLE_ENDIAN16(hhcb,pValue,dmaStruct,offset)  \
            HIM_GET_LITTLE_ENDIAN16((hhcb)->osdHandle,(pValue),(dmaStruct),(offset))
#define SCSI_GET_LITTLE_ENDIAN24(hhcb,pValue,dmaStruct,offset)  \
            HIM_GET_LITTLE_ENDIAN24((hhcb)->osdHandle,(pValue),(dmaStruct),(offset))
#define SCSI_GET_LITTLE_ENDIAN32(hhcb,pValue,dmaStruct,offset)  \
            HIM_GET_LITTLE_ENDIAN32((hhcb)->osdHandle,(pValue),(dmaStruct),(offset))
#define SCSI_GET_LITTLE_ENDIAN64(hhcb,pValue,dmaStruct,offset)  \
            HIM_GET_LITTLE_ENDIAN64((hhcb)->osdHandle,(pValue),(dmaStruct),(offset))

#if SCSI_SIMULATION_SUPPORT

/* Support switchable 32/64 bit scatter gather list format */
#define SCSI_GET_LITTLE_ENDIAN64SGPAD(hhcb,pValue,dmaStruct,offset)  \
            HIM_GET_LITTLE_ENDIAN32((hhcb)->osdHandle,((HIM_UEXACT32 *)pValue)+1,(dmaStruct),(offset));\
            *((HIM_UEXACT32 *)pValue) = (HIM_UEXACT32)0            

/* Support switchable 32/64 bit scatter gather list format */
#define SCSI_PUT_LITTLE_ENDIAN64SG(hhcb,dmaStruct,offset,value)  \
                         HIM_PUT_LITTLE_ENDIAN32((hhcb)->osdHandle,(dmaStruct),(offset),\
                                       *(((HIM_UEXACT32 *)&value)+1));

#else

/* Support switchable 32/64 bit scatter gather list format */
#define SCSI_PUT_LITTLE_ENDIAN64SG(hhcb,dmaStruct,offset,value)  \
                         HIM_PUT_LITTLE_ENDIAN32((hhcb)->osdHandle,(dmaStruct),(offset),\
                                       *((HIM_UEXACT32 *)&value));

/* Support switchable 32/64 bit scatter gather list format */
#define SCSI_GET_LITTLE_ENDIAN64SGPAD(hhcb,pValue,dmaStruct,offset)  \
            HIM_GET_LITTLE_ENDIAN32((hhcb)->osdHandle,(pValue),(dmaStruct),(offset));\
            *((HIM_UEXACT32 *)pValue+1) = (HIM_UEXACT32)0            

#endif /* SCSI_SIMULATION_SUPPORT */

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
/* Customization unique to implementation                                 */
/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
typedef int             SCSI_INT;      /* signed int                       */
typedef unsigned int    SCSI_UINT;     /* unsigned int                     */
typedef long            SCSI_LONG;     /* signed long                      */
typedef unsigned long   SCSI_ULONG;    /* unsigned long                    */

typedef struct SCSI_SG_DESCRIPTOR_
{
    SCSI_BUS_ADDRESS busAddress;
    SCSI_UEXACT32    segmentLength;
} SCSI_SG_DESCRIPTOR;

/***************************************************************************
* Definitions for hardware register access
***************************************************************************/
#define SCSI_REGISTER               SCSI_IO_HANDLE HIM_PTR
#define SCSI_AICREG(a)              (a)
#define OSD_INEXACT8(reg) (HIM_UEXACT8) scsiRegister->OSMReadUExact8(scsiRegister->ioHandle,(HIM_UINT32)(reg))
#define OSD_INEXACT16(reg) (HIM_UEXACT16) scsiRegister->OSMReadUExact16(scsiRegister->ioHandle,(HIM_UINT32)(reg))
#define OSD_OUTEXACT8(reg,value) scsiRegister->OSMWriteUExact8(scsiRegister->ioHandle,(HIM_UINT32)(reg),(HIM_UEXACT8)(value))
#define OSD_OUTEXACT16(reg,value) scsiRegister->OSMWriteUExact16(scsiRegister->ioHandle,(HIM_UINT32)(reg),(HIM_UEXACT16)(value))
#define	OSD_SYNCHRONIZE_IOS(hhcb)
#define OSD_INEXACT8_HIGH(reg)   OSDInExact8High((scsiRegister),(SCSI_UINT32)(reg)) 
#define OSD_OUTEXACT8_HIGH(reg,value) OSDOutExact8High((scsiRegister),(SCSI_UINT32)(reg),(SCSI_UEXACT8)(value))

#define OSD_INEXACT16_HIGH(reg)  OSDInExact16High((scsiRegister),(SCSI_UINT32)(reg)) 
#define OSD_OUTEXACT16_HIGH(reg,value) OSDOutExact16High((scsiRegister),(SCSI_UINT32)(reg),(SCSI_UEXACT16)(value))
/***************************************************************************
* Definitions for OSM support routines
***************************************************************************/
#define  OSD_GET_BUS_ADDRESS(hhcb,category,virtualAddress) \
                  OSDGetBusAddress((hhcb),(category),(virtualAddress))
#define  OSD_ADJUST_BUS_ADDRESS(hhcb,busAddress,value) \
                  OSDAdjustBusAddress((hhcb),(busAddress),(value))
#if !SCSI_DCH_U320_MODE
#define  OSD_READ_PCICFG_EXACT32(hhcb,registerNumber) \
                  OSDReadPciConfiguration((hhcb),(registerNumber))
#define  OSD_WRITE_PCICFG_EXACT32(hhcb,registerNumber,value) \
                  OSDWritePciConfiguration((hhcb),(registerNumber),(value))
#endif /* !SCSI_DCH_U320_MODE */

#define  OSD_COMPLETE_HIOB(hiob) OSDCompleteHIOB((hiob))
#define  OSD_COMPLETE_SPECIAL_HIOB(hiob) OSDCompleteSpecialHIOB((hiob))
#define  OSD_ASYNC_EVENT(hhcb,event) OSDAsyncEvent((hhcb),(event))
#define  OSD_BUILD_SCB(hiob) OSDBuildSCB((hiob))
#define  OSD_GET_SG_LIST(hiob) OSDGetSGList((hiob))
#define  OSD_GET_HOST_ADDRESS(hhcb) OSDGetHostAddress((hhcb))
#define  OSD_DELAY(hhcb,microSeconds) OSDDelay((hhcb),(microSeconds))
#define  OSD_GET_NVDATA(hhcb,destination,sourceoffset,length) \
                  OSDGetNVData((hhcb),(destination),(sourceoffset),(length))

/***************************************************************************
* Definitions for DV support routines
***************************************************************************/

#define SCSI_DV_STANDARD_MODE         0 /* 0 - SM DV, no header or linking  */
#define SCSI_DV_TARGET_COLLISION_MODE 1 /* 1 - SM DV, no header or linking  */
#define SCSI_DV_LOOSE_HEADER_MODE     2 /* 2 - SM DV, with loose header     */
#define SCSI_DV_STRICT_HEADER_MODE    3 /* 3 - SM DV, with strict header    */

/***************************************************************************
* Definitions for Target Mode OSM support routines
***************************************************************************/
#define  OSD_BUILD_TARGET_SCB(hiob) OSDBuildTargetSCB((hiob))
#define  OSD_ALLOCATE_NODE(hhcb) OSDAllocateNODE((hhcb))

/***************************************************************************
* Prototype for functions defined by OSM
***************************************************************************/
SCSI_BUS_ADDRESS OSDGetBusAddress(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,void SCSI_HPTR);
void OSDDelay(SCSI_HHCB SCSI_HPTR,SCSI_UINT32);
void OSDAdjustBusAddress(SCSI_HHCB SCSI_HPTR,SCSI_BUS_ADDRESS SCSI_HPTR,SCSI_UINT);

#if !SCSI_DCH_U320_MODE
SCSI_UEXACT32 OSDReadPciConfiguration(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void OSDWritePciConfiguration(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT32);
#endif /* !SCSI_DCH_U320_MODE */

void OSDCompleteHIOB(SCSI_HIOB SCSI_IPTR);
void OSDCompleteSpecialHIOB(SCSI_HIOB SCSI_IPTR);
void OSDAsyncEvent(SCSI_HHCB SCSI_HPTR,SCSI_UINT16,...);
void OSDBuildSCB(SCSI_HIOB SCSI_IPTR);
SCSI_BUFFER_DESCRIPTOR HIM_PTR OSDGetSGList (SCSI_HIOB SCSI_IPTR);
SCSI_HOST_ADDRESS SCSI_LPTR OSDGetHostAddress(SCSI_HHCB SCSI_HPTR hhcb);
void OSDSynchronizeIOs(SCSI_HHCB SCSI_HPTR);
SCSI_UINT8 OSDGetNVData (SCSI_HHCB SCSI_HPTR, void HIM_PTR, HIM_UINT32, HIM_UINT32);
SCSI_UEXACT8 OSDInExact8High(SCSI_REGISTER,SCSI_UINT32);
void OSDOutExact8High(SCSI_REGISTER,SCSI_UINT32,SCSI_UEXACT8);
SCSI_UEXACT16 OSDInExact16High(SCSI_REGISTER,SCSI_UINT32);
void OSDOutExact16High(SCSI_REGISTER,SCSI_UINT32,SCSI_UEXACT16);
void OSDSynchronizeRange(SCSI_REGISTER);
#if SCSI_PACKETIZED_IO_SUPPORT           
#if (SCSI_TASK_SWITCH_SUPPORT == 2)
SCSI_UEXACT8 OSDGetTargetCmd (SCSI_HIOB SCSI_IPTR );
#endif /* SCSI_TASK_SWITCH_SUPPORT == 2 */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
/***************************************************************************
* Prototype for Target Mode functions defined by OSM
***************************************************************************/
SCSI_SCB_DESCRIPTOR SCSI_HPTR OSDGetFreeHead(SCSI_HHCB SCSI_HPTR);
void OSDBuildTargetSCB(SCSI_HIOB SCSI_IPTR);
SCSI_NODE SCSI_NPTR OSDAllocateNODE(SCSI_HHCB SCSI_HPTR);



#ifdef	__cplusplus
}
#endif


#endif /* _SCSICHIM_H */

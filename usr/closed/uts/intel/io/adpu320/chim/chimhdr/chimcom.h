/* $Header: /vobs/u320chim/src/chim/chimhdr/chimcom.h   /main/6   Mon Mar 17 18:18:16 2003   quan $ */

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
*  Module Name:   CHIMCOM.H
*
*  Description:   Definitions which are common to all implementations
*
*  Owners:        ECX IC Firmware Team
*
*  Notes:         Contains basic data structures 
*                    (quadlet, him_bus_address, him_host_id, etc.)
*                 Contains #defines for pseudo-enum values
*                    (All s/g defines, host bus types, 
*                     memory categories, etc.)
*
***************************************************************************/


/***************************************************************************
* Definitions for two byte entity
***************************************************************************/
#if OSM_CPU_LITTLE_ENDIAN
    typedef union HIM_DOUBLET_                  /* two orders entity              */
    {
        struct
        {
            HIM_UEXACT8  order0;
            HIM_UEXACT8  order1;
        } u8;
    } HIM_DOUBLET;
#else
    typedef union HIM_DOUBLET_                  /* two orders entity              */
    {
        struct
        {
            HIM_UEXACT8  order1;
            HIM_UEXACT8  order0;
        } u8;
    } HIM_DOUBLET;
#endif


/***************************************************************************
* Definitions for four byte entity
***************************************************************************/
#if OSM_CPU_LITTLE_ENDIAN
    typedef union HIM_QUADLET_                  /* four orders entity             */
    {
        struct
        {
            HIM_UEXACT8  order0;
            HIM_UEXACT8  order1;
            HIM_UEXACT8  order2;
            HIM_UEXACT8  order3;
        } u8;
        struct
        {
            HIM_UEXACT16 order0;
            HIM_UEXACT16 order1;
        } u16;
    } HIM_QUADLET;
#else
    typedef union HIM_QUADLET_                  /* four orders entity             */
    {
        struct
        {
            HIM_UEXACT8  order3;
            HIM_UEXACT8  order2;
            HIM_UEXACT8  order1;
            HIM_UEXACT8  order0;
        } u8;
        struct
        {
            HIM_UEXACT16 order1;
            HIM_UEXACT16 order0;
        } u16;
    } HIM_QUADLET;
#endif


/***************************************************************************
* Definitions for eight byte entity
***************************************************************************/
#if OSM_CPU_LITTLE_ENDIAN
    typedef union HIM_OCTLET_                  /* 8 orders entity                */
    {
        struct
        {
            HIM_UEXACT8 order0;
            HIM_UEXACT8 order1;
            HIM_UEXACT8 order2;
            HIM_UEXACT8 order3;
            HIM_UEXACT8 order4;
            HIM_UEXACT8 order5;
            HIM_UEXACT8 order6;
            HIM_UEXACT8 order7;
        } u8;
        struct
        {
            HIM_UEXACT16 order0;
            HIM_UEXACT16 order1;
            HIM_UEXACT16 order2;
            HIM_UEXACT16 order3;
        } u16;
        struct
        {
            HIM_UEXACT32 order0;
            HIM_UEXACT32 order1;
        } u32;
    } HIM_OCTLET;
#else
    typedef union HIM_OCTLET_                  /* 8 orders entity                */
    {
        struct
        {
#if OSM_CPU_ADDRESS_SIZE == 32 
/* 32 bit CPU addressing */
            HIM_UEXACT8 order3;
            HIM_UEXACT8 order2;
            HIM_UEXACT8 order1;
            HIM_UEXACT8 order0;
            HIM_UEXACT8 order7;
            HIM_UEXACT8 order6;
            HIM_UEXACT8 order5;
            HIM_UEXACT8 order4;
#else
/* 64 bit CPU addressing */
            HIM_UEXACT8 order7;
            HIM_UEXACT8 order6;
            HIM_UEXACT8 order5;
            HIM_UEXACT8 order4;
            HIM_UEXACT8 order3;
            HIM_UEXACT8 order2;
            HIM_UEXACT8 order1;
            HIM_UEXACT8 order0;
#endif /* OSM_CPU_ADDRESS_SIZE == 32 */
        } u8;
        struct
        {
#if OSM_CPU_ADDRESS_SIZE == 32
/* 32 bit CPU addressing */
            HIM_UEXACT16 order1;
            HIM_UEXACT16 order0;
            HIM_UEXACT16 order3;
            HIM_UEXACT16 order2;
#else
/* 64 bit CPU addressing */
            HIM_UEXACT16 order3;
            HIM_UEXACT16 order2;
            HIM_UEXACT16 order1;
            HIM_UEXACT16 order0;
#endif /* OSM_CPU_ADDRESS_SIZE == 32 */
        } u16;
        struct
        {
#if OSM_CPU_ADDRESS_SIZE == 32 
/* 32 bit CPU addressing */
            HIM_UEXACT32 order0;
            HIM_UEXACT32 order1;
#else
/* 64 bit CPU addressing */
            HIM_UEXACT32 order1;
            HIM_UEXACT32 order0;
#endif /* OSM_CPU_ADDRESS_SIZE == 32 */ 
        } u32;
    } HIM_OCTLET;
#endif


/***************************************************************************
* Buffer descriptor
***************************************************************************/
typedef struct HIM_BUFFER_DESCRIPTOR_
{
    HIM_BUS_ADDRESS busAddress;          /* bus address                   */
    HIM_UEXACT32    bufferSize;          /* scatter/gather count          */
    void HIM_PTR    virtualAddress;      /* virtual address for iob       */
} HIM_BUFFER_DESCRIPTOR;


/***************************************************************************
* Miscellaneous definitions
***************************************************************************/
typedef HIM_UEXACT32 HIM_HOST_ID;

typedef union HIM_HOST_ADDRESS_
{
    struct HIM_PCI_ADDRESS_
    {  /* PCI address */
        HIM_UINT8 treeNumber;               /* hierarchy                     */
        HIM_UINT8 busNumber;                /* PCI bus number                */
        HIM_UINT8 deviceNumber;             /* PCI device number             */
        HIM_UINT8 functionNumber;           /* PCI function number           */
    } pciAddress;

    struct HIM_EISA_ADDRESS_
    {  /* EISA address */
        HIM_UINT8 slotNumber;
    } eisaAddress;

    struct HIM_DCH_RBI_ADDRESS
    {  /* RBI address */
        HIM_UINT32 tbdInformation;        /* unknown units of storage        */
    } rbiAddress;
    
} HIM_HOST_ADDRESS;

/***************************************************************************
 * host bus type definitions 
 ***************************************************************************/
#define HIM_HOST_BUS_PCI     0              /* PCI bus                       */
#define HIM_HOST_BUS_EISA    1              /* EISA bus                      */

/***************************************************************************
* Memory category code definition
***************************************************************************/
#define  HIM_MC_UNLOCKED   0        /* unlocked, not DMAable, cached */
#define  HIM_MC_LOCKED     1        /* locked, DMAable memory, non-cached */

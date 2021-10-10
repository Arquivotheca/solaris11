/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * $Header: /vobs/u320chim/src/chim/chimhdr/chimosm.h_t   /main/8   Mon Dec  9 13:08:47 2002   luu $
 */
/*
 * Copyright 1995,1996,1997,1998,1999,2000,2001,2002 Adaptec, Inc.,         *
 * All Rights Reserved.                                                     *
 *                                                                          *
 * This software contains the valuable trade secrets of Adaptec.  The       *
 * software is protected under copyright laws as an unpublished work of     *
 * Adaptec.  Notice is for informational purposes only and does not imply   *
 * publication.  The user of this software may make copies of the software  *
 * for use with parts manufactured by Adaptec or under license from Adaptec *
 * and for no other use.                                                    *
 */

#ifndef	_CHIMOSM_H
#define	_CHIMOSM_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/***************************************************************************
*
*  Module Name:   CHIMOSM.H
*
*  Description:   Definitions for customization by OSM writer
*
*  Owners:        Template provided by ECX IC Firmware Team
*
*  Notes:         Contains the following, as required by the 
*                 CHIM Specification (See Section 12.1 if desired):
*                    him_include_xxx: Enables him-specific .h's.
*                    system-specific #defines
*                       OSM_CPU_LITTLE_ENDIAN, 
*                       OSM_BUS_ADDRESS_SIZE, etc.
*                    OSM-specified types
*                       HIM_PTR (near or far)
*                       HIM_UINT8, HIM_UEXACT16, etc.
*                       HIM_BUS_ADDRESS
*                       HIM_BOOLEAN
*                       HIM_IO_HANDLE
*                    macros
*                       HIM_FLUSH_CACHE, etc.
*                       OSMmemset, etc.
*                       OSMoffsetof, OSMAssert, OSMDebugPrint,
*                           OSMEnterDebugger   
*                       HIM_PUT_BIG_ENDIAN, etc.
*
***************************************************************************/

#include <sys/int_types.h>

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
/* HIM's to be included                                                   */
/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
#define HIM_INCLUDE_SCSI

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
/* OEM1's to be included.  It is comment out by default                   */
/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
/*
 * #define HIM_OEM1_INCLUDE_SCSI
 */

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
/* Endian options                                                         */
/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

#define OSM_CPU_LITTLE_ENDIAN  1 /*  0 - big    endian                  */
                                 /*  1 - little endian                  */
#define OSM_DMA_SWAP_ENDIAN    0 /*  0 - no swapping                    */
                                 /*  1 - swap every osm_swap_width      */
                                 /* -1 - swap by other means            */
#ifdef HIM_64ADDR
#define OSM_DMA_SWAP_WIDTH    64 /* Number of swapping bits.  Also      */
                                 /* used by the HIM as a DMA-alignment  */
                                 /* requirement.                        */
#define OSM_BUS_ADDRESS_SIZE  64 /* Number of bits in bus address       */
#else
#define OSM_DMA_SWAP_WIDTH    32 /* Number of swapping bits.  Also      */
                                 /* used by the HIM as a DMA-alignment  */
                                 /* requirement.                        */
#define OSM_BUS_ADDRESS_SIZE  32 /* Number of bits in bus address       */
#endif

                                 /* Number of bits in CPU addressing    */
                                 /* Normally OSM_BUS_ADDRESS_SIZE and
                                  * OSM_CPU_ADDRESS_SIZE are the same
                                  * value. However, it is important 
                                  * to set these defines correctly
                                  * when using a 32 bit big endian CPU
                                  * in a 64 bit bus address environment.
                                  * I.e. OSM_BUS_ADDRESS_SIZE   64
                                  *      OSM_CPU_ADDRESS_SIZE   32 
                                  */ 
#define OSM_CPU_ADDRESS_SIZE  OSM_BUS_ADDRESS_SIZE

/***************************************************************************
* Definition for memory pointer reference
***************************************************************************/
#define  HIM_PTR    *  /* The pointer passed to all HIM calls and structs */

/***************************************************************************
* Data types
***************************************************************************/
#if 0   /* The following are examples of the optimum size for each */
    typedef char            HIM_SINT8;    /* signed   int  - min of   8       */
    typedef unsigned char   HIM_UINT8;    /* unsigned int  - min of   8       */
    typedef char            HIM_SEXACT8;  /* signed   byte - exactly  8       */
    typedef unsigned char   HIM_UEXACT8;  /* unsigned byte - exactly  8       */

    typedef short           HIM_SINT16;   /* signed   int  - min of   16      */
    typedef unsigned short  HIM_UINT16;   /* unsigned int  - min of   16      */
    typedef short           HIM_SEXACT16; /* signed   word - exactly  16      */
    typedef unsigned short  HIM_UEXACT16; /* unsigned word - exactly  16      */

    typedef long            HIM_SINT32;   /* signed   int   - min of  32      */
    typedef unsigned long   HIM_UINT32;   /* unsigned int   - min of  32      */
    typedef long            HIM_SEXACT32; /* signed   dword - exactly 32      */
    typedef unsigned long   HIM_UEXACT32; /* unsigned dword - exactly 32      */
#endif

/* Sizes optimized for speed */
typedef int            HIM_SINT8;    /* signed   int  - min of   8       */
typedef unsigned int   HIM_UINT8;    /* unsigned int  - min of   8       */
typedef int8_t         HIM_SEXACT8;  /* signed   byte - exactly  8       */
typedef uint8_t        HIM_UEXACT8;  /* unsigned byte - exactly  8       */

typedef int            HIM_SINT16;   /* signed   int  - min of   16      */
typedef unsigned int   HIM_UINT16;   /* unsigned int  - min of   16      */
typedef int16_t        HIM_SEXACT16; /* signed   word - exactly  16      */
typedef uint16_t       HIM_UEXACT16; /* unsigned word - exactly  16      */

typedef int            HIM_SINT32;   /* signed   int   - min of  32      */
typedef unsigned int   HIM_UINT32;   /* unsigned int   - min of  32      */
typedef int32_t        HIM_SEXACT32; /* signed   dword - exactly 32      */
typedef uint32_t       HIM_UEXACT32; /* unsigned dword - exactly 32      */

/* Sizes added for 64-bit compatible */
typedef unsigned long HIM_ULONG;     /* unsigned long - 32 on 32-bit platform; 64 on 64-bit platform   */ 

/***************************************************************************
* Define a BUS ADDRESS (Physical address)
***************************************************************************/
#ifdef HIM_64ADDR
typedef unsigned long long HIM_BUS_ADDRESS;
#else
typedef unsigned long HIM_BUS_ADDRESS;
#endif
/*
typedef struct HIM_BUS_ADDRESS_
{
   HIM_UEXACT32 u32entity0;
   HIM_UEXACT32 u32entity1;
} HIM_BUS_ADDRESS;
*/
/***************************************************************************
* Define a boolean 
***************************************************************************/
typedef enum HIM_BOOLEAN_ENUM { HIM_FALSE, HIM_TRUE } HIM_BOOLEAN; /* boolean */

/***************************************************************************
* Definitions for the I/O handle
***************************************************************************/
typedef struct HIM_IO_HANDLE_
{
    HIM_ULONG       baseAddress; /* raw base address */
    HIM_UEXACT32    attributes;  /* mostly same as him #defines */
    struct adpu320_config_   *cfp;
} HIM_IO_HANDLE;

/***************************************************************************
* Definitions for cache flushing and invalidating
***************************************************************************/
/* Intel platform -- define as null */
#define  HIM_FLUSH_CACHE(pMemory,length)
#define  HIM_INVALIDATE_CACHE(pMemory,length)


/***************************************************************************
* Definitions for standard ANSI-C functions
***************************************************************************/
/*
#define  OSMmemset(buf,value,length)   memset((char HIM_PTR)(buf),(char) (value),(int) (length))
#define  OSMmemcmp(buf1,buf2,length)   memcmp((char HIM_PTR)(buf1),(char HIM_PTR)(buf2),(int) (length))
#define  OSMmemcpy(buf1,buf2,length)   memcpy((char HIM_PTR)(buf1),(char HIM_PTR)(buf2),(int) (length))
*/

extern void OSMmemset(void *ptr, int val, int len);
extern void OSMmemcpy(void *dest, void *src,  int len);
extern int  OSMmemcmp(void *ptr1, void *ptr2, int len);

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
/* miscellaneous system utilities */
/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
#define OSMoffsetof(structDef,member)  (HIM_UEXACT16)((HIM_UEXACT32)&(((structDef *)0)->member))
#define OSMAssert(x)                   if(!(x)) EnterDebugger();

/* By default OSMDebugPrint is not defined, as we do not want any performance */
/* impact.  A sample definition has been provided below to illustrate the     */
/* usage of 'priority' variable. */

#define OSMDebugPrint(priority,string)

/*
 * Sample definition of OSMDebugPrint.
 *
 * extern  HIM_UINT32 OSMDebugMode;
 * extern  long       ScreenHandle;
 *
 * #define OSMDebugPrint(priority,string) if ((priority) <= OSMDebugMode) \
 *                                         OutputToScreen(ScreenHandle,string);
 */

#define OSMEnterDebugger()             EnterDebugger();

/* The following define is invoked by the CHIM to return
 * OSMEvent information related to the OSMEvent when the 
 * event value is HIM_EVENT_HA_FAILED or HIM_EVENT_IO_CHANNEL_FAILED.
 */

#define OSMDebugReportEventInfo(osmAdapterContext,eventInfo) \
	printf("%X badseq CallerID %X\n",osmAdapterContext,eventInfo)

/*PRINTFLIKE1*/
extern void printf(const char *, ...);

/*
 * Sample definition
 *
 * void HIM_PTR osmAdapterContext;
 * HIM_UINT32 eventInfo; 
 *
 * #define OSMDebugReportEventInfo(osmAdapterContext,eventInfo)  \
 *           OSMLogCallerId((osmAdapterContext),(eventInfo))
 */


/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
/* Endian-transformation macros    */
/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
#if OSM_CPU_LITTLE_ENDIAN
/* Little endian versions of the macros */
#define HIM_PUT_BYTE_STRING(osmAdapterContext,dmaStruct,offset,pSrc,length)\
           OSMmemcpy(((HIM_UEXACT8 *)(dmaStruct))+(offset),    \
                     (HIM_UEXACT8 *)pSrc,                      \
                     length)

#define HIM_PUT_LITTLE_ENDIAN8(osmAdapterContext,dmaStruct,offset,value)  \
            *(((HIM_UEXACT8 *)(dmaStruct))+(offset)) = (HIM_UEXACT8)(value)

#define HIM_PUT_LITTLE_ENDIAN16(osmAdapterContext,dmaStruct,offset,value) \
            *((HIM_UEXACT16 *)(((HIM_UEXACT8 *)\
               (dmaStruct))+(offset))) = (HIM_UEXACT16)(value)

#define HIM_PUT_LITTLE_ENDIAN24(osmAdapterContext,dmaStruct,offset,value) \
            *((HIM_UEXACT32 *)(((HIM_UEXACT8 *)(dmaStruct))+(offset))) &= \
                        0xFF000000; \
            *((HIM_UEXACT32 *)(((HIM_UEXACT8 *)(dmaStruct))+(offset))) |= \
                        ((HIM_UEXACT32)(value)) & 0x00FFFFFF

#define HIM_PUT_LITTLE_ENDIAN32(osmAdapterContext,dmaStruct,offset,value) \
            *((HIM_UEXACT32 *)(((HIM_UEXACT8 *)\
               (dmaStruct))+(offset))) = (HIM_UEXACT32)(value)
                                       
#define HIM_PUT_LITTLE_ENDIAN64(osmAdapterContext,dmaStruct,offset,value) \
            HIM_PUT_LITTLE_ENDIAN32((osmAdapterContext),(dmaStruct),(offset),\
                                    *((HIM_UEXACT32 *)&value));\
            HIM_PUT_LITTLE_ENDIAN32((osmAdapterContext),(dmaStruct),(offset)+4,\
                                    *(((HIM_UEXACT32 *)&value)+1))

#define HIM_GET_BYTE_STRING(osmAdapterContext,pDest,dmaStruct,offset,length)\
           OSMmemcpy((HIM_UEXACT8 *)pDest,                      \
                     ((HIM_UEXACT8 *)(dmaStruct))+(offset),     \
                     length)

#define HIM_GET_LITTLE_ENDIAN8(osmAdapterContext,pValue,dmaStruct,offset)  \
            *((HIM_UEXACT8 *)pValue) = *(((HIM_UEXACT8 *)(dmaStruct))+(offset))

#define HIM_GET_LITTLE_ENDIAN16(osmAdapterContext,pValue,dmaStruct,offset) \
            *((HIM_UEXACT16 *)pValue) =\
               *((HIM_UEXACT16 *)(((HIM_UEXACT8 *)(dmaStruct))+(offset)))
                                                            
#define HIM_GET_LITTLE_ENDIAN24(osmAdapterContext,pValue,dmaStruct,offset) \
            *((HIM_UEXACT32 *)pValue) = *((HIM_UEXACT32 *)\
                  (((HIM_UEXACT8 *)(dmaStruct))+(offset))) & 0x00FFFFFF
                        
#define HIM_GET_LITTLE_ENDIAN32(osmAdapterContext,pValue,dmaStruct,offset) \
            *((HIM_UEXACT32 *)pValue) =  *((HIM_UEXACT32 *)\
                                 (((HIM_UEXACT8 *)(dmaStruct))+(offset)))

#define HIM_GET_LITTLE_ENDIAN64(osmAdapterContext,pValue,dmaStruct,offset) \
            HIM_GET_LITTLE_ENDIAN32((osmAdapterContext),(pValue),(dmaStruct),(offset));\
            HIM_GET_LITTLE_ENDIAN32((osmAdapterContext),((HIM_UEXACT32 *)\
                                       (pValue))+1,(dmaStruct),offset+4)

#else
/* Big endian versions of the macros */
/* Note that; these versions of macros may not be efficient for most
 * big endian systems. They were created for a CHIM simulation environment.
 */
#define HIM_PUT_BYTE_STRING(osmAdapterContext,dmaStruct,offset,pSrc,length)\
           OSMmemcpy(((HIM_UEXACT8 *)(dmaStruct))+(offset),    \
                     (HIM_UEXACT8 *)pSrc,                      \
                     length)

#define HIM_PUT_LITTLE_ENDIAN8(osmAdapterContext,dmaStruct,offset,value)  \
            *(((HIM_UEXACT8 *)(dmaStruct))+(offset)) = (HIM_UEXACT8)(value)

#define HIM_PUT_LITTLE_ENDIAN16(osmAdapterContext,dmaStruct,offset,value) \
            HIM_PUT_LITTLE_ENDIAN8((osmAdapterContext),(dmaStruct),(offset),\
                                   (HIM_UEXACT8)(value));\
            HIM_PUT_LITTLE_ENDIAN8((osmAdapterContext),(dmaStruct),(offset)+1,\
                                   (HIM_UEXACT8)(value >> 8))

#define HIM_PUT_LITTLE_ENDIAN24(osmAdapterContext,dmaStruct,offset,value) \
            HIM_PUT_LITTLE_ENDIAN8((osmAdapterContext),(dmaStruct),(offset),\
                                   (HIM_UEXACT8)(value));\
            HIM_PUT_LITTLE_ENDIAN8((osmAdapterContext),(dmaStruct),(offset)+1,\
                                   (HIM_UEXACT8)(value >> 8));\
            HIM_PUT_LITTLE_ENDIAN8((osmAdapterContext),(dmaStruct),(offset)+2,\
                                   (HIM_UEXACT8)(value>>16))

#define HIM_PUT_LITTLE_ENDIAN32(osmAdapterContext,dmaStruct,offset,value) \
            HIM_PUT_LITTLE_ENDIAN16(osmAdapterContext,dmaStruct,offset,\
                                    (HIM_UEXACT16)(value));\
            HIM_PUT_LITTLE_ENDIAN16(osmAdapterContext,dmaStruct,offset+2,\
                                    (HIM_UEXACT16)((HIM_UEXACT32)value >> 16))
                                       
#define HIM_PUT_LITTLE_ENDIAN64(osmAdapterContext,dmaStruct,offset,value) \
            HIM_PUT_LITTLE_ENDIAN32((osmAdapterContext),(dmaStruct),(offset),\
                                    (HIM_UEXACT32)(value));\
            HIM_PUT_LITTLE_ENDIAN32((osmAdapterContext),(dmaStruct),(offset)+4,\
                                    (HIM_UEXACT32)((HIM_UEXACT32)value >> 32)))
                                       
#define HIM_GET_BYTE_STRING(osmAdapterContext,pDest,dmaStruct,offset,length)\
           OSMmemcpy((HIM_UEXACT8 *)pDest,                      \
                     ((HIM_UEXACT8 *)(dmaStruct))+(offset),     \
                     length)

#define HIM_GET_LITTLE_ENDIAN8(osmAdapterContext,pValue,dmaStruct,offset)  \
            *((HIM_UEXACT8 *)pValue) = *(((HIM_UEXACT8 *)(dmaStruct))+(offset))

#define HIM_GET_LITTLE_ENDIAN16(osmAdapterContext,pValue,dmaStruct,offset) \
            HIM_GET_LITTLE_ENDIAN8((osmAdapterContext),((HIM_UEXACT8 *)\
                                    (pValue))+1,(dmaStruct),(offset));\
            HIM_GET_LITTLE_ENDIAN8((osmAdapterContext),(pValue),(dmaStruct),(offset)+1)
                                                            

#define HIM_GET_LITTLE_ENDIAN24(osmAdapterContext,pValue,dmaStruct,offset) \
            HIM_GET_LITTLE_ENDIAN8((osmAdapterContext),((HIM_UEXACT8 *)(pValue))+2,\
                                   (dmaStruct),(offset));\
            HIM_GET_LITTLE_ENDIAN8((osmAdapterContext),((HIM_UEXACT8 *)(pValue))+1,\
                                   (dmaStruct),(offset)+1);\
            HIM_GET_LITTLE_ENDIAN8((osmAdapterContext),(pValue),(dmaStruct),(offset)+2)
                        
#define HIM_GET_LITTLE_ENDIAN32(osmAdapterContext,pValue,dmaStruct,offset) \
            HIM_GET_LITTLE_ENDIAN16((osmAdapterContext),((HIM_UEXACT16 *)(pValue))+1,\
                                    (dmaStruct),(offset));\
            HIM_GET_LITTLE_ENDIAN16((osmAdapterContext),((HIM_UEXACT16 *)(pValue)),\
                                    (dmaStruct),(offset)+2)

#define HIM_GET_LITTLE_ENDIAN64(osmAdapterContext,pValue,dmaStruct,offset) \
            HIM_GET_LITTLE_ENDIAN32((osmAdapterContext),((HIM_UEXACT32 *)(pValue))+1,\
                                    (dmaStruct),(offset));\
            HIM_GET_LITTLE_ENDIAN32((osmAdapterContext),((HIM_UEXACT32 *)(pValue)),\
                                    (dmaStruct),(offset)+4)

#endif /* OSM_CPU_LITTLE_ENDIAN */

/* Parallel SCSI CHIM does not call any Big Endian macros. However, they are
 * defined for completeness. 
 */
#define HIM_PUT_BIG_ENDIAN8(osmAdapterContext,dmaStruct,offset,value)\
            *(((HIM_UEXACT8 *)(dmaStruct))+(offset)) = (HIM_UEXACT8)(value)

#define HIM_PUT_BIG_ENDIAN16(osmAdapterContext,dmaStruct,offset,value)\
            HIM_PUT_BIG_ENDIAN8((osmAdapterContext),(dmaStruct),(offset),\
                                (HIM_UEXACT8)(value >> 8));\
            HIM_PUT_BIG_ENDIAN8((osmAdapterContext),(dmaStruct),(offset)+1,\
                                (HIM_UEXACT8)(value))

#define HIM_PUT_BIG_ENDIAN24(osmAdapterContext,dmaStruct,offset,value)\
            HIM_PUT_BIG_ENDIAN8((osmAdapterContext),(dmaStruct),(offset),\
                                (HIM_UEXACT8)(value >> 16));\
            HIM_PUT_BIG_ENDIAN8((osmAdapterContext),(dmaStruct),(offset)+1,\
                                (HIM_UEXACT8)(value >> 8));\
            HIM_PUT_BIG_ENDIAN8((osmAdapterContext),(dmaStruct),(offset)+2,\
                                (HIM_UEXACT8)(value))

#define HIM_PUT_BIG_ENDIAN32(osmAdapterContext,dmaStruct,offset,value)\
            HIM_PUT_BIG_ENDIAN16(osmAdapterContext,dmaStruct,offset,\
                                 (HIM_UEXACT16)(value >> 16));\
            HIM_PUT_BIG_ENDIAN16(osmAdapterContext,dmaStruct,offset+2,\
                                 (HIM_UEXACT16)(value))

#define HIM_PUT_BIG_ENDIAN64(osmAdapterContext,dmaStruct,offset,value) \
            HIM_PUT_BIG_ENDIAN32((osmAdapterContext),(dmaStruct),(offset),\
                                 *(((HIM_UEXACT32 *)&value)+1));\
            HIM_PUT_BIG_ENDIAN32((osmAdapterContext),(dmaStruct),(offset)+4,\
                                 *(((HIM_UEXACT32 *)&value)+0))

#define HIM_GET_BIG_ENDIAN8(osmAdapterContext,pValue,dmaStruct,offset)\
            *((HIM_UEXACT8 *)pValue) = *(((HIM_UEXACT8 *)(dmaStruct))+\
                                                            (offset))

#define HIM_GET_BIG_ENDIAN16(osmAdapterContext,pValue,dmaStruct,offset)\
            HIM_GET_BIG_ENDIAN8((osmAdapterContext),((HIM_UEXACT8 *)\
                                   (pValue))+1,(dmaStruct),(offset));\
            HIM_GET_BIG_ENDIAN8((osmAdapterContext),(pValue),(dmaStruct),(offset)+1)

#define HIM_GET_BIG_ENDIAN24(osmAdapterContext,pValue,dmaStruct,offset)\
            HIM_GET_BIG_ENDIAN8((osmAdapterContext),((HIM_UEXACT8 *)(pValue))+2,\
                                (dmaStruct),(offset));\
            HIM_GET_BIG_ENDIAN8((osmAdapterContext),((HIM_UEXACT8 *)(pValue))+1,\
                                (dmaStruct),(offset)+1);\
            HIM_GET_BIG_ENDIAN8((osmAdapterContext),(pValue),(dmaStruct),(offset)+2)

#define HIM_GET_BIG_ENDIAN32(osmAdapterContext,pValue,dmaStruct,offset)\
            HIM_GET_BIG_ENDIAN16((osmAdapterContext),((HIM_UEXACT16 *)(pValue))+1,\
                                 (dmaStruct),(offset));\
            HIM_GET_BIG_ENDIAN16((osmAdapterContext),((HIM_UEXACT16 *)(pValue)),\
                                 (dmaStruct),(offset)+2)

#define HIM_GET_BIG_ENDIAN64(osmAdapterContext,pValue,dmaStruct,offset)\
            HIM_GET_BIG_ENDIAN32((osmAdapterContext),((HIM_UEXACT32 *)(pValue))+1,\
                                 (dmaStruct),(offset));\
            HIM_GET_BIG_ENDIAN32((osmAdapterContext),((HIM_UEXACT32 *)(pValue)),\
                                 (dmaStruct),(offset)+4)

#ifdef	__cplusplus
}
#endif

#endif /* _CHIMOSM_H */

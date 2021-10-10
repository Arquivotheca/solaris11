/****************************************************************************
 * Copyright(c) 2000 Broadcom Corporation, all rights reserved
 * Proprietary and Confidential Information.
 *
 * This source file is the property of Broadcom Corporation, and
 * may not be copied or distributed in any isomorphic form without
 * the prior written consent of Broadcom Corporation.
 *
 * Name: bcmtype.h
 *
 * Description: Cross-Processor/OS/Compiler data type definition
 *
 * Author: ffan
 *
 * $Log: bcmtype.h,v $
 * Revision 1.3  2007/03/04 12:51:09  alone
 * added OPTIONAL macro
 *
 * Revision 1.2  2006/03/16 14:21:22  vladz
 * After the first stage of the integration with the lm-device.
 *
 * Revision 1.1  2006/03/07 11:47:56  eilong
 * Adding the MCP (BC1 to start with)
 *
 *
 * 6     3/25/04 3:30p Ffan
 *
 * 3     1/17/02 2:24p Ffan
 * Added Watcom DOS4G support.
 *
 * 1     11/28/01 7:41p Ffan
 * Initial check-in.
 *
 * 1     10/22/01 3:47p Ffan
 * Revision 1.1  2001/07/12 01:42:38  ffan
 * Initial revision.
 *
 *
 * 1     4/30/00 2:05p Ffan
 * Initial check-in. Both outbound and inbound load-balance and
 * failover are working.
 *
 ****************************************************************************/
#ifndef __bcmtype_h__
#define __bcmtype_h__

#if defined(UEFI) && defined (EVEREST_DIAG)
#include <machine/endian.h>
#endif

#ifndef IN
#define IN
#endif /* IN */

#ifndef OUT
#define OUT
#endif /* OUT */

#ifndef INOUT
#define INOUT
#endif /* INOUT */

#ifndef OPTIONAL
#define OPTIONAL
#endif /* OPTIONAL */

#if defined(__LINUX) || defined (USER_LINUX)

#ifdef __LINUX

#ifdef __BIG_ENDIAN
#ifndef BIG_ENDIAN
#define BIG_ENDIAN
#endif
#else /* __LITTLE_ENDIAN */
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN
#endif
#endif

/*
 * define underscore-t types
 */
typedef u64 u64_t;
typedef u32 u32_t;
typedef u16 u16_t;
typedef u8  u8_t;

typedef s64 s64_t;
typedef s32 s32_t;
typedef s16 s16_t;
typedef s8  s8_t;

typedef unsigned long int_ptr_t;

#else /* USER_LINUX */

#if __BYTE_ORDER == __LITTLE_ENDIAN
#undef BIG_ENDIAN
#undef __BIG_ENDIAN
#else
#undef LITTLE_ENDIAN
#undef __LITTLE_ENDIAN
#endif

/*
 * define underscore-t types
 */
typedef u_int64_t u64_t;
typedef u_int32_t u32_t;
typedef u_int16_t u16_t;
typedef u_int8_t  u8_t;

typedef int64_t s64_t;
typedef int32_t s32_t;
typedef int16_t s16_t;
typedef int8_t  s8_t;

typedef u_int64_t u64;
typedef u_int32_t u32;
typedef u_int16_t u16;
typedef u_int8_t  u8;

typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t  s8;

typedef unsigned long int_ptr_t;

/* Define upper case types */

typedef u64_t 	U64;
typedef u32_t 	U32;
typedef u16_t 	U16;
typedef u8_t  	U8;

typedef s64_t	S64;
typedef s32_t	S32;
typedef s16_t	S16;
typedef s8_t	S8;

#endif



#else
/*
 * define the data model
 */
#if !defined(LP64) && !defined(P64) && !defined(LLP64)
  /* VC 32-bit compiler 5.0 or later */
  #if (_MSC_VER > 800)
    #define P64
  #elif defined(__sun)
    /* Solaris */
    #define LP64
  #elif defined(_HPUX_SOURCE)
    /* HP/UX */
    #define LP64
  #elif defined(__FreeBSD__)
    /* FreeBSD */
    #define LP64
  #elif defined(LINUX)
    /* Linux */
    #define LP64
  #elif defined(__bsdi__)
    /* BSDI */
    #define LP64
  #elif defined(_IRIX)
    /* IRIX */
    #define LP64
  #elif defined(UNIXWARE)
    /* UnixWare */
    #define LP64
  #endif /* UNIXWARE */
#endif /* !LP64 && !P64 && !LLP64 */

/*
 * define sized type
 */
#if defined(P64) || defined(LLP64)
  /* P64 */
  typedef unsigned __int64    U64;
  typedef unsigned int        U32;
  typedef unsigned short      U16;
  typedef unsigned char       U8;
  typedef signed __int64      S64;
  typedef signed int          S32;
  typedef signed short        S16;
  typedef signed char         S8;

  #if defined(IA64)  || defined(AMD64)
        typedef U64  int_ptr_t;
  #else   
    #ifndef UEFI64    
        typedef unsigned long       int_ptr_t; 
    #endif
  #endif
#elif defined(LP64)
  /* LP64: Sun, HP and etc */
  typedef unsigned long long  U64;
  typedef unsigned int        U32;
  typedef unsigned short      U16;
  typedef unsigned char       U8;
  typedef signed long long    S64;
  typedef signed int          S32;
  typedef signed short        S16;
  typedef signed char         S8;
  typedef unsigned long       int_ptr_t; 
#elif defined(__WATCOMC__) 
  typedef unsigned __int64    U64;
  typedef unsigned long       U32;
  typedef unsigned short      U16;
  typedef unsigned char       U8;
  typedef signed __int64      S64;
  typedef signed long         S32;
  typedef signed short        S16;
  typedef signed char         S8;
  typedef unsigned long       int_ptr_t;  
#else
  /* assume others: 16-bit */
  typedef unsigned char       U64[8];
  typedef unsigned long       U32;
  typedef unsigned short      U16;
  typedef unsigned char       U8;
  typedef signed char         S64[8];
  typedef signed long         S32;
  typedef signed short        S16;
  typedef signed char         S8;     
  typedef unsigned long       int_ptr_t;  
#endif /*  */

 

/*
 * define lower case types
 */
typedef U64 u64_t;
typedef U32 u32_t;
typedef U16 u16_t;
typedef U8  u8_t;

typedef S64 s64_t;
typedef S32 s32_t;
typedef S16 s16_t;
typedef S8  s8_t;

#ifndef LINUX
typedef U64 u64;
typedef U32 u32;
typedef U16 u16;
typedef U8  u8;

typedef S64 s64;
typedef S32 s32;
typedef S16 s16;
typedef S8  s8;
#endif

#endif

#ifdef UEFI
#if BYTE_ORDER == LITTLE_ENDIAN
#undef BIG_ENDIAN
#endif
#ifdef UEFI64
typedef u64_t  int_ptr_t;
#endif
#endif

#ifdef LITTLE_ENDIAN
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#endif /* __LITTLE_ENDIAN */
#endif /* LITTLE_ENDIAN */
#ifdef BIG_ENDIAN
#ifndef __BIG_ENDIAN
#define  __BIG_ENDIAN  BIG_ENDIAN
#endif /* __BIG_ENDIAN */
#endif /* BIG_ENDIAN */

/* Signed subtraction macros with no sign extending.  */
#define S64_SUB(_a, _b)     ((s64_t) ((s64_t) (_a) - (s64_t) (_b)))
#define u64_SUB(_a, _b)     ((u64_t) ((s64_t) (_a) - (s64_t) (_b)))
#define S32_SUB(_a, _b)     ((s32_t) ((s32_t) (_a) - (s32_t) (_b)))
#define uS32_SUB(_a, _b)    ((u32_t) ((s32_t) (_a) - (s32_t) (_b)))
#define S16_SUB(_a, _b)     ((s16_t) ((s16_t) (_a) - (s16_t) (_b)))
#define u16_SUB(_a, _b)     ((u16_t) ((s16_t) (_a) - (s16_t) (_b)))
#define PTR_SUB(_a, _b)     ((u8_t *) (_a) - (u8_t *) (_b))

#endif/* __bcmtype_h__ */


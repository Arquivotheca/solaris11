/*******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2001-2007 Broadcom Corporation, ALL RIGHTS RESERVED.
 *
 *
 * Module Description:
 *  This file should include pure ANSI C defines
 *
 * History:
 *    04/03/07 Alon Elhanani        Inception.
 ******************************************************************************/

#ifndef __utils_h__
#define __utils_h__

/*
XXX_FLAGS
bitwise flags operations, for readability of the code
*/

// get specific flags
#define GET_FLAGS(flags,bits)       ((flags) & (bits))
// set specific flags
#define SET_FLAGS(flags,bits)       ((flags) |= (bits))
// reset specific flags
#define RESET_FLAGS(flags,bits)     ((flags) &= ~(bits))
// clear flags
#define CLEAR_FLAGS(flags)          ((flags)=0)

// macros for a single bit
#define SET_BIT( _bits, _val )   SET_FLAGS  ( _bits,   (1 << _val) )
#define RESET_BIT( _bits, _val ) RESET_FLAGS( _bits,   (1 << _val) )
#define GET_BIT( _bits, _val )   GET_FLAGS  ( _bits,   (1 << _val) )

/*
ARRSIZE:
used to calcualte item count of an array
this macro is used to prevent compile warning for unreferenced parametes
*/
#ifndef ARRSIZE
#define ARRSIZE(a)                   (sizeof(a)/sizeof((a)[0]))
#endif // ARRSIZE

/*
UNREFERENCED_PARAMETER
this macro is used to prevent compile warning for unreferenced parametes
*/
#ifndef UNREFERENCED_PARAMETER_
#define UNREFERENCED_PARAMETER_(P)\
    /*lint -save -e527 -e530 */  \
    { \
        (P) = (P); \
    }
#endif // UNREFERENCED_PARAMETER_


/*
ASSERT_STATIC
this macro is used to raise COMPILE time assertions
e.g: ASSERT_STATIC( sizeof(S08) == 1 )
relevant errors that compilers gives in such case:
build.exe (MS)     - "error  C2196: case value '0' already used"
WMAKE.exe (Watcom) - "Error! E1039: Duplicate case value '0' found"
*/
#ifndef ASSERT_STATIC
#define ASSERT_STATIC(cond) \
    {   \
        const unsigned char dummy_zero = 0 ; \
        switch(dummy_zero){case 0:case (cond):;} \
    }
#ifdef __SUNPRO_C /* Sun's cc can't deal with this clever hack */
#undef ASSERT_STATIC
#define ASSERT_STATIC(cond)
#endif
#endif // ASSERT_STATIC(cond)

/*
RANGE
this macro is used to check that a certain variable is within a given range
e.g: RANGE_INCLUDE(a, 10, 100)    - is the following true : 10<=a<=100
*/
#define IN_RANGE(_var, _min, _max)  ( ((_var) >= (_min)) && ((_var) <= (_max)) )

/*
Define standard min and max macros
*/
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#if defined(__LINUX) || defined(USER_LINUX)
#undef max
#endif // LINUX

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif // !max

/*
Define for pragma message output with file name and line number
usage: #pragma message (MSGSTR("blah blah blah"))
*/
#define _STR(x) #x
#define _STR2(x) _STR(x)
#define MSGSTR(msg) __FILE__ "(" _STR2(__LINE__)"): message - " ##msg


#ifndef POWER_OF_2
// known algorithm
#define POWER_OF_2(x)       ((0 != x) && (0 == (x &(x-1))))
#endif // !POWER_OF_2

#ifndef FAST_PATH_MODULO
// a = b (mod n)
// If a==b the compiler will omit the last line.
#define FAST_PATH_MODULO(a,b,n)     \
    do                              \
    {                               \
        while ((b) > ((n) -1))      \
            (b) = (b) - (n);        \
        (a)=(b);                    \
    }                               \
    while(0)
#endif // !FAST_PATH_MODULO

#ifndef MINCHAR
#define MINCHAR     0x80
#endif
#ifndef MAXCHAR
#define MAXCHAR     0x7f
#endif
#ifndef MINSHORT
#define MINSHORT    0x8000
#endif
#ifndef MAXSHORT
#define MAXSHORT    0x7fff
#endif
#ifndef MINLONG
#define MINLONG     0x80000000
#endif
#ifndef MAXLONG
#define MAXLONG     0x7fffffff
#endif
#ifndef MAXBYTE
#define MAXBYTE     0xff
#endif
#ifndef MAXWORD
#define MAXWORD     0xffff
#endif
#ifndef MAXDWORD
#define MAXDWORD    0xffffffff
#endif

#define CLEAR_MSB32(_val32) (_val32 & MAXLONG)

#endif /* __utils_h__ */

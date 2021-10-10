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
 *    25/04/10 Shay Haroush        Inception.
 ******************************************************************************/
#ifndef CYCLIC_OPERATIONS
#define CYCLIC_OPERATIONS

/********	Cyclic Operators Macros	********/

#define _ABS_DIFF(x, y) ((x) > (y) ? (x) - (y) : (y) - (x))

static __inline u8_t _cyclic_lt(u32_t x, u32_t y, u32_t d)
{
	u32_t diff = _ABS_DIFF(x,y);
	return (diff < d) ? x < y : x > y;
}

static __inline u8_t _cyclic_le(u32_t x, u32_t y, u32_t d)
{
	u32_t diff = _ABS_DIFF(x,y);
	return (diff < d) ? x <= y : x >= y;
}

#define CYCLIC_LT_8(x, y)  (_cyclic_lt(x, y, 128))
#define CYCLIC_LT_16(x, y) (_cyclic_lt(x, y, 32768))
#define CYCLIC_LT_24(x, y) (_cyclic_lt(x, y, 8388608))
#define CYCLIC_LT_32(x, y) (_cyclic_lt(x, y, 2147483648))

#define CYCLIC_LE_8(x, y)  (_cyclic_le(x, y, 128))
#define CYCLIC_LE_16(x, y) (_cyclic_le(x, y, 32768))
#define CYCLIC_LE_24(x, y) (_cyclic_le(x, y, 8388608))
#define CYCLIC_LE_32(x, y) (_cyclic_le(x, y, 2147483648))

#define CYCLIC_GT_8(x, y)  (!(CYCLIC_LE_8(x, y)))
#define CYCLIC_GT_16(x, y) (!(CYCLIC_LE_16(x, y)))
#define CYCLIC_GT_24(x, y) (!(CYCLIC_LE_24(x, y)))
#define CYCLIC_GT_32(x, y) (!(CYCLIC_LE_32(x, y)))

#define CYCLIC_GE_8(x, y)  (!(CYCLIC_LT_8(x, y)))
#define CYCLIC_GE_16(x, y) (!(CYCLIC_LT_16(x, y)))
#define CYCLIC_GE_24(x, y) (!(CYCLIC_LT_24(x, y)))
#define CYCLIC_GE_32(x, y) (!(CYCLIC_LT_32(x, y)))

// bits = number of bits in x, y (i.e., sizeof_x)
#define CYCLIC_LT_BITS(x, y, bits)	_cyclic_lt(x, y, 1 << ((bits)-1))
#define CYCLIC_LE_BITS(x, y, bits)	_cyclic_le(x, y, 1 << ((bits)-1))
#define CYCLIC_GT_BITS(x, y, bits)	(!(CYCLIC_LE_BITS(x, y, bits)))
#define CYCLIC_GE_BITS(x, y, bits)	(!(CYCLIC_LT_BITS(x, y, bits)))

/********	End	Cyclic Operators Macros	********/

#endif // CYCLIC_OPERATIONS

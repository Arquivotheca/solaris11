/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2006-2008 Broadcom Corporation, ALL RIGHTS RESERVED.
 ******************************************************************************/


#ifndef	_MM_H_
#define	_MM_H_

#include "bnx_mm.h"

/*
 * Add some preprocessor definitions that
 * should technically be part of the LM.
 */
#define LM_HC_RX_QUICK_CONS_TRIP_VAL_MAX        256
#define LM_HC_RX_QUICK_CONS_TRIP_INT_MAX        256

#define LM_HC_RX_TICKS_VAL_MAX                  511
#define LM_HC_RX_TICKS_INT_MAX                  1023


#define mm_indicate_rx( a, b, c, d )

#endif /* _MM_H_ */

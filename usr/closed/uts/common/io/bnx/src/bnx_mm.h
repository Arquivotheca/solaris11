/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2006-2010 Broadcom Corporation, ALL RIGHTS RESERVED.
 ******************************************************************************/


#ifndef BNX_MM_H
#define BNX_MM_H

#include <atomic.h>

#define mm_read_barrier() membar_consumer()
#define mm_write_barrier() membar_producer()

#include "lm.h"
#include "lm5706.h"

#define FLUSHPOSTEDWRITES(_lmdevice)                                \
    {                                                               \
        volatile uint32_t dummy;                                    \
        REG_RD((_lmdevice), pci_config.pcicfg_int_ack_cmd, &dummy); \
    }

#endif	/* BNX_MM_H */


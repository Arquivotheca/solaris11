/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2007-2010 Broadcom Corporation, ALL RIGHTS RESERVED.
 ******************************************************************************/


#ifndef BNX_TMR_H
#define BNX_TMR_H

#include "bnx.h"

void bnx_timer_init( um_device_t * const umdevice );
void bnx_timer_start( um_device_t * const umdevice );
void bnx_timer_stop( um_device_t * const umdevice );
void bnx_timer_fini( um_device_t * const umdevice );
void bnx_link_timer_restart( um_device_t * umdevice );

#endif /* BNX_TMR_H */

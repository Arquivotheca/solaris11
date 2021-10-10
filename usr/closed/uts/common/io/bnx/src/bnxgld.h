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


#ifndef BNX_GLD_H
#define BNX_GLD_H

#include "bnx.h"

int  bnx_gld_init( um_device_t * const umdevice );
void bnx_gld_link( um_device_t * const umdevice, const link_state_t linkup );
int  bnx_gld_fini( um_device_t * const umdevice );

#endif /* BNX_GLD_H */

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


#ifndef BNX_CFG_H
#define BNX_CFG_H

#include "bnx.h"

extern const bnx_lnk_cfg_t bnx_copper_config;
extern const bnx_lnk_cfg_t bnx_serdes_config;

void bnx_cfg_msix( um_device_t * const umdevice );
void bnx_cfg_init( um_device_t * const umdevice );
void bnx_cfg_reset( um_device_t * const umdevice );
void bnx_cfg_map_phy( um_device_t * const umdevice );

#endif /* BNX_CFG_H */

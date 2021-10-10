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


#ifndef BNX_NDD_H
#define BNX_NDD_H

#include "bnx.h"

int  bnx_ndd_init( um_device_t * const umdevice );
void bnx_ndd_fini( um_device_t * const umdevice );

enum ioc_reply
bnx_nd_ioctl(
	um_device_t *udevp,
	queue_t *wq,
	mblk_t *mp,
	struct iocblk *iocp
	);

#endif /* BNX_NDD_H */

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


#ifndef BNX_SND_H
#define BNX_SND_H

#include "bnx.h"

int  bnx_txpkts_init( um_device_t * const umdevice );
void bnx_txpkts_flush( um_device_t * const umdevice );
void bnx_txpkts_fini( um_device_t * const umdevice );

#define BNX_SEND_GOODXMIT  0
#define BNX_SEND_LINKDOWN  1
#define BNX_SEND_DEFERPKT  2
#define BNX_SEND_HDWRFULL  3

int  bnx_xmit_pkt_map( um_txpacket_t * const umpacket, mblk_t * mp );

int
bnx_xmit_ring_xmit_qpkt( um_device_t * const umdevice,
                         const unsigned int ringidx );

int
bnx_xmit_ring_xmit_mblk( um_device_t * const umdevice,
                         const unsigned int ringidx,
                         mblk_t * mp );

void
bnx_xmit_ring_reclaim( um_device_t * const umdevice,
                       const unsigned int ringidx, s_list_t * srcq );

void
bnx_xmit_ring_intr( um_device_t * const umdevice,
                    const unsigned int  ringidx );

void
bnx_txpkts_intr( um_device_t * const umdevice );

void
bnx_xmit_ring_post( um_device_t * const umdevice, const unsigned int ringidx );

#endif /* BNX_SND_H */

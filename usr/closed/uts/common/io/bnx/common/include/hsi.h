/******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2007-2008 Broadcom Corporation, ALL RIGHTS RESERVED.
 ******************************************************************************/

#ifndef PRIVATE_HSI_H
#define PRIVATE_HSI_H

#define tcp_syn_dos_defense		(0x10 + 0x020)
#define rxp_unicast_bytes_rcvd		(0x10 + 0x0d0)
#define rxp_multicast_bytes_rcvd	(0x10 + 0x0d8)
#define rxp_broadcast_bytes_rcvd	(0x10 + 0x0e0)
#define RXP_HSI_OFFSETOFF(x)		(x)

#define com_no_buffer			(0x10 + 0x074)
#define COM_HSI_OFFSETOFF(x)		(x)

#define unicast_bytes_xmit		(0x410 + 0x030)
#define multicast_bytes_xmit		(0x410 + 0x038)
#define broadcast_bytes_xmit		(0x410 + 0x040)
#define TPAT_HSI_OFFSETOFF(x)		(x)

#endif

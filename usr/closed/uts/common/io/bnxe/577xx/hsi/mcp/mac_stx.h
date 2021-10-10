/****************************************************************************
 * Copyright(c) 2006-2008 Broadcom Corporation, all rights reserved
 * Proprietary and Confidential Information.
 *
 * Name: mac_stx.h
 *
 * Description: Host collected MAC statistics
 *
 * Author: Yitchak Gertner
 *
 * $Date: 2010/11/29 $       $Revision: #3 $
 ****************************************************************************/

#ifndef MAC_STX_H
#define MAC_STX_H


#include "mac_stats.h"


#define MAC_STX_NA                          0xffffffff


typedef struct emac_stats emac_stats_t;
typedef struct bmac1_stats bmac1_stats_t;
typedef struct bmac2_stats bmac2_stats_t;
typedef union  mac_stats mac_stats_t;
typedef struct mac_stx mac_stx_t;
typedef struct host_port_stats host_port_stats_t;
typedef struct host_func_stats host_func_stats_t;


#endif /* MAC_STX_H */


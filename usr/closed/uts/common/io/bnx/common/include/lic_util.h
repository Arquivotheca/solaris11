/****************************************************************************
 * Copyright(c) 2006-2007 Broadcom Corporation, all rights reserved
 * Proprietary and Confidential Information.
 *
 * This source file is the property of Broadcom Corporation, and
 * may not be copied or distributed in any isomorphic form without
 * the prior written consent of Broadcom Corporation.
 *
 * Name:        lic_util.h
 *
 * Description: 
 *
 * Created:     03/01/2006 vsiu
 *
 ****************************************************************************/

#ifndef _LIC_UTIL_H
#define _LIC_UTIL_H

#include "bcmtype.h"
#include "tcl.h"

typedef struct _cfg_entry_t
{
    u32_t idx;
    u8_t desc[80];
    u8_t keyword[10];
    u8_t u_is_str;
    u8_t str_val[20]; /* meaningful when u_is_str==1 */
    u32_t val32;      /* the equivalent value of the str_val */
} cfg_entry_t;

#define CFG_TOE_CONN                             0
#define CFG_RDMA_CONN                            1
#define CFG_ISCSI_INIT_CONN                      2
#define CFG_ISCSI_TRGT_CONN                      3
#define CFG_ISER_INIT_CONN                       4
#define CFG_ISER_TRGT_CONN                       5
#define CFG_ISCSI_BOOT                           6
#define CFG_ISCSI_FULL                           7
#define CFG_ISCSI_HDR                            8
#define CFG_ISCSI_BODY                           9
#define CFG_SERIAL_NUM                           10
#define CFG_EXP_DATE                             11
#define CFG_SERDES_2_5G                          12
#define CFG_MAX_ENTRY                            13

#endif /* _LIC_UTIL_H */

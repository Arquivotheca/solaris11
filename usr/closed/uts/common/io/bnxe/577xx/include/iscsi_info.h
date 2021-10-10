/******************************************************************************/
/* Broadcom Corporation.  No part of this file may be reproduced or           */
/* distributed, in any form or by any means for any purpose, without the      */
/* express written permission of Broadcom Corporation.                        */
/*                                                                            */
/* (c) COPYRIGHT 2000-2004 Broadcom Corporation, ALL RIGHTS RESERVED.         */
/*                                                                            */
/*  FILE        :  I S C S I _ I N F O . H                                    */
/*                                                                            */
/*  AUTHOR      :  Kevin Tran                                                 */
/*                                                                            */
/*  DESCRIPTION :  This file contains macro definitions for ISCSI shared      */
/*                 information.                                               */
/*                                                                            */
/*  Revision History:                                                         */
/*    Kevin Tran         07/23/2004       Created                             */
/*                                                                            */
/******************************************************************************/

#ifndef __ISCSI_INFO_H__
#define __ISCSI_INFO_H__

typedef struct _iscsi_info_block_hdr_t
{
#define ISCSI_INFO_BLOCK_SIGNATURE 0x6b627369 
  u32_t signature;
  u16_t length;
  u8_t  checksum;
  u8_t  boot_flags;
    #define ISCSI_INFO_FLAGS_WINDOWS_HBA_BOOT  (1 << 0)  
} iscsi_info_block_hdr_t;

#define ISCSI_INFO_DATA_SIZE  1016

typedef struct _iscsi_info_block_t
{
  iscsi_info_block_hdr_t hdr ;
  u8_t                   data[ISCSI_INFO_DATA_SIZE];
} iscsi_info_block_t;

#define ISCSI_INFO_ID_TARGET_NAME     0x0
#define ISCSI_INFO_ID_TARGET_IP_ADDR  0x1
#define ISCSI_INFO_ID_TARGET_TCP_PORT 0x2
#define ISCSI_INFO_ID_INITIATOR_NAME  0x3
#define ISCSI_INFO_ID_IP_ADDRESS      0x4
#define ISCSI_INFO_ID_SUBNET_MASK     0x5
#define ISCSI_INFO_ID_DEFAULT_GATEWAY 0x6
#define ISCSI_INFO_ID_AUTHEN_MODE     0x7
#define ISCSI_INFO_ID_INIT_CHAP_ID    0x8
#define ISCSI_INFO_ID_INIT_CHAP_PW    0x9
#define ISCSI_INFO_ID_TARGET_CHAP_ID  0xa
#define ISCSI_INFO_ID_TARGET_CHAP_PW  0xb
#define ISCSI_INFO_ID_VLAN_ID         0xc
#define ISCSI_INFO_ID_PRIMARY_DNS     0xd
#define ISCSI_INFO_ID_SECONDARY_DNS   0xe
#define ISCSI_INFO_ID_INTF_MAC_ADDR   0xf

#define ISCSI_INFO_ID_IP_ADDRESS2      0x10
#define ISCSI_INFO_ID_SUBNET_MASK2     0x11
#define ISCSI_INFO_ID_DEFAULT_GATEWAY2 0x12
#define ISCSI_INFO_ID_VLAN_ID2         0x13
#define ISCSI_INFO_ID_INTF_MAC_ADDR2   0x14
#define ISCSI_INFO_ID_PRIMARY_DNS2     0x15
#define ISCSI_INFO_ID_SECONDARY_DNS2   0x16
#define ISCSI_INFO_ID_DHCP_SERVER_IP_ADDR 0x17
#define ISCSI_INFO_ID_BOOT_INTF        0x18
#define ISCSI_INFO_ID_TARGET_LUN_NUM   0x19
#define ISCSI_INFO_ID_PRIMARY_INTF     0x1a 
#define ISCSI_INFO_ID_PCI_FUNCTION_NUM 0x1b
#define ISCSI_INFO_ID_DHCP_SERVER_IP_ADDR2 0x1c
#define ISCSI_INFO_ID_TARGET_IP_ADDR2  0x1d
#define ISCSI_INFO_ID_TARGET_TCP_PORT2 0x1e
#define ISCSI_INFO_ID_BOOT_PARAMETERS  0x1f
#define ISCSI_INFO_ID_BOOT_PARAMETERS2 0x20

#define ISCSI_INFO_ID_END             0xff

#define ISCSI_INFO_BOOT_PARAMS_IPv6_RA   (1 << 0)

typedef struct iscsi_info_hdr
{
  u16_t id;
  u16_t length;
}iscsi_info_hdr;

typedef struct iscsi_info_entry
{
  iscsi_info_hdr hdr;
  u8_t data[1];
}iscsi_info_entry;

#endif /* __ISCSI_INFO_H__ */

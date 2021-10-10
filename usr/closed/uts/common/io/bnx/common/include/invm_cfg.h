/******************************************************************************/
/* Broadcom Corporation.  No part of this file may be reproduced or           */
/* distributed, in any form or by any means for any purpose, without the      */
/* express written permission of Broadcom Corporation.                        */
/*                                                                            */
/* (c) COPYRIGHT 2000-2009 Broadcom Corporation, ALL RIGHTS RESERVED.         */
/*                                                                            */
/*  FILE        :  I N V M _ C F G . H                                        */
/*                                                                            */
/*  AUTHOR      :  Kevin Tran                                                 */
/*                                                                            */
/*  DESCRIPTION :  This file contains global macros definitions.              */
/*                                                                            */
/*  Revision History:                                                         */
/*    Kevin Tran         10/10/2005       Created                             */
/*                                                                            */
/******************************************************************************/

#ifndef __INVM_CFG_H__
#define __INVM_CFG_H__

#include "itypes.h"
#include "iparms.h"
#include <stdio.h> 
#include <string.h>

#define NVM_ISCSI_CFG_BLOCK_SIZE    1024
#define NVM_ISCSI_CFG_V2_BLOCK_SIZE 2048 

#define HOST_TO_NET16(x) (u16_t)((((x) >> 8) & 0xff) | (((x) << 8) & 0xff00))
#define HOST_TO_NET32(x) swap_dword(x)
#define NET_TO_HOST(x) (u16_t)((((x) >> 8) & 0xff) | (((x) << 8) & 0xff00))
#define NET_TO_HOST16(x) (u16_t)((((x) >> 8) & 0xff) | (((x) << 8) & 0xff00))
#define NET_TO_HOST32(x) swap_dword(x)

#pragma pack(push,1)

/* 0x0 to 0x7f */
typedef struct NVM_ISCSI_GENERAL_INFO
{
  u16_t signature;
  #define NVM_ISCSI_BLOCK_SIGNATURE  0x6962 /* "ib" */ 
  u16_t version;
  #define NVM_ISCSI_FORMAT_VERSION_MAJOR     1
  #define NVM_ISCSI_FORMAT_VERSION_MAJOR_TWO 2   /* With IPv6 support */
  #define NVM_ISCSI_FORMAT_VERSION_MINOR   0

  #define NVM_ISCSI_FORMAT_VERSION (NVM_ISCSI_FORMAT_VERSION_MAJOR << 8) |\
    NVM_ISCSI_FORMAT_VERSION_MINOR

  #define NVM_ISCSI_FORMAT_VERSION_2 (NVM_ISCSI_FORMAT_VERSION_MAJOR_TWO << 8) |\
    NVM_ISCSI_FORMAT_VERSION_MINOR

  u32_t ctrl_flags;
   #define NVM_ISCSI_FLAGS_CHAP_ENABLE          (1 << 0)
   #define NVM_ISCSI_FLAGS_DHCP_TCPIP_CONFIG    (1 << 1)
   #define NVM_ISCSI_FLAGS_DHCP_ISCSI_CONFIG    (1 << 2)
   #define NVM_ISCSI_FLAGS_BOOT_TO_TARGET       (1 << 3)
   #define NVM_ISCSI_FLAGS_USE_PRIMARY_MAC_ADDR (1 << 4)
   #define NVM_ISCSI_FLAGS_USE_TCP_TIMESTAMP    (1 << 5)
   #define NVM_ISCSI_FLAGS_USE_TARGET_HDD_FIRST (1 << 6)
   #define NVM_ISCSI_FLAGS_WINDOWS_HBA_MODE     (1 << 7)
   #define NVM_ISCSI_FLAGS_IPV6_SUPPORT         (1 << 8)

  char dhcp_vendor_id[ISCSI_MAX_DHCP_VENDOR_ID_LENGTH+1];
  u8_t link_up_delay;                /* Offset -- 0x29 */
  u8_t lun_busy_retry_count;         /* Offset -- 0x2a */
  u8_t reserved[85];                 /* Offset -- 0x2b to 0x7f */
}NVM_ISCSI_GENERAL_INFO;

/* 0x80 to 0xA7 */
typedef struct NVM_ISCSI_SECONDARY_NIC_INFO
{
  u16_t ctrl_flags;                              /* 0x80 */
   #define NVM_ISCSI_FLAGS_MPIO_MODE_ENABLE     (1 << 0)
   #define NVM_ISCSI_FLAGS_USE_SEC_TARGET_INFO  (1 << 1)
   #define NVM_ISCSI_FLAGS_USE_SEC_TARGET_NAME  (1 << 2)
  u8_t mac_address[6]; /* MAC address of secondary interface -- 0x82 */
  u8_t reserved[32];   /* Offset 0x88 ... 0xa7 */
}NVM_ISCSI_SECONDARY_NIC_INFO,*pNVM_ISCSI_SECONDARY_NIC_INFO;

typedef struct NVM_ISCSI_INITIATOR_INFO
{
  IP4_ADDR ip_addr;            /* 0xA8 */
  IP4_ADDR subnet_mask;        /* 0xAC */ 
  IP4_ADDR gateway;            /* 0xB0 */ 
  char iscsi_name[ISCSI_MAX_ISCSI_NAME_LENGTH+1];
  char chap_id[ISCSI_MAX_CHAP_ID_SLENGTH+1];
  char chap_password[ISCSI_MAX_CHAP_PW_LENGTH+1];
  IP4_ADDR primary_dns;            
  IP4_ADDR secondary_dns;            
  u8_t reserved[57];  
}NVM_ISCSI_INITIATOR_INFO,*pNVM_ISCSI_INITIATOR_INFO;

/* First Target  : 0x1A8 to 0x2A7 */
/* Second Target : 0x2A8 to 0x2A7 */
typedef struct NVM_ISCSI_TARGET_INFO
{
  u16_t ctrl_flags;
   #define NVM_ISCSI_FLAGS_TARGET_ENABLE    (1 << 0)
  u16_t tcp_port;
  IP4_ADDR ip_addr;
  u16_t boot_lun;
  char chap_id[ISCSI_MAX_CHAP_ID_SLENGTH+1];
  char chap_password[ISCSI_MAX_CHAP_PW_LENGTH+1];
  char iscsi_name[ISCSI_MAX_ISCSI_NAME_LENGTH+1];
  u8_t reserved[67];  
}NVM_ISCSI_TARGET_INFO,*pNVM_ISCSI_TARGET_INFO;

typedef struct NVM_ISCSI_INITIATOR_INFO_V2
{
  IP_ADDR ip_addr;            /* 0xA8 ... 0xb7 */
  IP_ADDR subnet_mask;        /* 0xb8 ... 0xc7 */ 
  IP_ADDR gateway;            /* 0xc8 ... 0xd7 */ 
  IP_ADDR primary_dns;        /* 0xd8 ... 0xe7 */     
  IP_ADDR secondary_dns;      /* 0xe8 ... 0xf7 */
  char iscsi_name[ISCSI_MAX_ISCSI_NAME_LENGTH+1]; /* 0xf8 ... 0x178 */
  char chap_id[ISCSI_MAX_CHAP_ID_LENGTH+1];       /* 0x179 ... 0x1f9 */
  char chap_password[ISCSI_MAX_CHAP_PW_LENGTH+1]; /* 0x1fa ... 0x20a */
  u8_t reserved[181];        /* 0x20b ... 0x2bf */
}NVM_ISCSI_INITIATOR_INFO_V2,*pNVM_ISCSI_INITIATOR_INFO_V2;

/* First Target  : 0x2c0 to 0x47f */
/* Second Target : 0x480 to 0x63f */
typedef struct NVM_ISCSI_TARGET_INFO_V2
{
  u16_t ctrl_flags;          /* 0x2c0 ... 0x2c1 */
   #define NVM_ISCSI_FLAGS_TARGET_ENABLE    (1 << 0)
  u16_t tcp_port;            /* 0x2c2 ... 0x2c3 */
  IP_ADDR ip_addr;           /* 0x2c4 ... 0x2d3 */
  u16_t boot_lun;            /* 0x2d4 ... 0x2d5 */
  char chap_id[ISCSI_MAX_CHAP_ID_LENGTH+1];        /* 0x2d6 ... 0x356 */
  char chap_password[ISCSI_MAX_CHAP_PW_LENGTH+1];  /* 0x257 ... 0x367 */
  char iscsi_name[ISCSI_MAX_ISCSI_NAME_LENGTH+1];  /* 0x368 ... 0x3e8 */
  u8_t reserved[279];                              /* 0x3e9 ... 0x4ff */
}NVM_ISCSI_TARGET_INFO_V2,*pNVM_ISCSI_TARGET_INFO_V2;

#define NVM_ISCSI_CFG_BLOCK_RESERVE_SIZE   NVM_ISCSI_CFG_BLOCK_SIZE - \
                                       (sizeof(NVM_ISCSI_GENERAL_INFO) +\
                        		sizeof(NVM_ISCSI_SECONDARY_NIC_INFO) + \
                                        sizeof(NVM_ISCSI_INITIATOR_INFO) + \
                                        sizeof(NVM_ISCSI_TARGET_INFO) + \
                                        sizeof(NVM_ISCSI_TARGET_INFO) + 4)
typedef struct NVM_ISCSI_CFG_BLOCK
{
  NVM_ISCSI_GENERAL_INFO gen;         /* 0x0 to 0x7F */
  NVM_ISCSI_SECONDARY_NIC_INFO secondary_device; /* 0x80 to 0xA7 */
  NVM_ISCSI_INITIATOR_INFO initiator;  /*  0xA8 to 0x1A7 */
  NVM_ISCSI_TARGET_INFO target1;       /*  0x1A8 to 0x2A7 */
  NVM_ISCSI_TARGET_INFO target2;       /*  0x2A8 to 0x2A7 */
  u8_t reserved[NVM_ISCSI_CFG_BLOCK_RESERVE_SIZE];  /* 0x3a8 to 0x3fb */
  u32_t crc32;
}NVM_ISCSI_CFG_BLOCK, *pNVM_ISCSI_CFG_BLOCK;

#define NVM_ISCSI_CFG_BLOCK_V2_RESERVE_SIZE   \
  NVM_ISCSI_CFG_V2_BLOCK_SIZE -						\
  (sizeof(NVM_ISCSI_GENERAL_INFO) +					\
   sizeof(NVM_ISCSI_SECONDARY_NIC_INFO) +				\
   sizeof(NVM_ISCSI_INITIATOR_INFO_V2) +  			        \
   sizeof(NVM_ISCSI_TARGET_INFO_V2) +					\
   sizeof(NVM_ISCSI_TARGET_INFO_V2) + 4)

typedef struct NVM_ISCSI_CFG_BLOCK_V2
{
  NVM_ISCSI_GENERAL_INFO gen;                          /* 0x0 to 0x7F */
  NVM_ISCSI_SECONDARY_NIC_INFO secondary_device;       /* 0x80 to 0xA7 */
  NVM_ISCSI_INITIATOR_INFO_V2 initiator;               /* 0xA8 to 0x2bf */
  NVM_ISCSI_TARGET_INFO_V2 target1; 	               /* 0x2c0 to 0x4ff */
  NVM_ISCSI_TARGET_INFO_V2 target2;                    /* 0x500 to 0x73f */
  u8_t reserved[NVM_ISCSI_CFG_BLOCK_V2_RESERVE_SIZE];  /* 0x740 to 0x7ff */
  u32_t crc32;
}NVM_ISCSI_CFG_BLOCK_V2, *pNVM_ISCSI_CFG_BLOCK_V2;

#pragma pack(pop)

int ib_migrate_cfgblk_v1_to_v2(pNVM_ISCSI_CFG_BLOCK iblk_v1,
                               pNVM_ISCSI_CFG_BLOCK_V2 iblk_v2,
                               int ipv6);
                               
int ib_migrate_cfgblk_v2_to_v1(pNVM_ISCSI_CFG_BLOCK_V2 iblk_v2,
                   pNVM_ISCSI_CFG_BLOCK iblk_v1);
                   
int ib_migrate_v2_cfgblk(pNVM_ISCSI_CFG_BLOCK_V2 iblk_v2,int ipv6_to_ipv4);

#endif /* __INVM_CFG_H__ */

/******************************************************************************/
/* Broadcom Corporation.  No part of this file may be reproduced or           */
/* distributed, in any form or by any means for any purpose, without the      */
/* express written permission of Broadcom Corporation.                        */
/*                                                                            */
/* (c) COPYRIGHT 2000-2009 Broadcom Corporation, ALL RIGHTS RESERVED.         */
/*                                                                            */
/*  FILE        :  T Y P E S . H                                              */
/*                                                                            */
/*  AUTHOR      :  Kevin Tran                                                 */
/*                                                                            */
/*  DESCRIPTION :  This file contains main ISCSI entry point.                 */
/*                                                                            */
/*  Revision History:                                                         */
/*    Kevin Tran         01/03/2004       Created                             */
/*                                                                            */
/******************************************************************************/
#ifndef __ITYPES_H__
#define __ITYPES_H__

#include "bcmtype.h"

typedef int  int_t;
typedef u16_t  bool_t;
typedef struct MAC_ADDR
{
  u8_t addr[6];
} MAC_ADDRESS, *pMAC_ADDRESS;

#define IP_ADDR_LEN     4

typedef union IP4_ADDR
{
  u32_t num;
  u8_t byte[IP_ADDR_LEN];
}IP4_ADDR,*pIP4_ADDR;

typedef union IPV6_ADDR
{
  u8_t  addr8[16];
  u16_t addr16[8];
  u32_t addr[4];
}IPV6_ADDR,*pIPV6_ADDR;

typedef union IP_ADDR
{
  IPV6_ADDR ipv6;
  IP4_ADDR ipv4;
}IP_ADDR,*pIP_ADDR;

#ifndef NULL
#define NULL 0
#endif

#ifndef TRUE 
#define TRUE  1
#endif

#ifndef FALSE 
#define FALSE  0
#endif

#endif /* __ITYPES_H__ */


/******************************************************************************/
/* Broadcom Corporation.  No part of this file may be reproduced or           */
/* distributed, in any form or by any means for any purpose, without the      */
/* express written permission of Broadcom Corporation.                        */
/*                                                                            */
/* (c) COPYRIGHT 2000-2009 Broadcom Corporation, ALL RIGHTS RESERVED.         */
/*                                                                            */
/*  FILE        :  I P A R M S . H                                            */
/*                                                                            */
/*  AUTHOR      :  Kevin Tran                                                 */
/*                                                                            */
/*  DESCRIPTION :  This file contains global macros definitions.              */
/*                                                                            */
/*  Revision History:                                                         */
/*    Kevin Tran         01/03/2004       Created                             */
/*                                                                            */
/******************************************************************************/

#ifndef __IPARMS_H__
#define __IPARMS_H__

#define ISCSI_HDR_BUF_SIZE          128

/* Size of user's receive buffer */
#define MAX_RX_PAYLOAD_SIZE     0xffffL

#define PKT_BUF_SIZE               1514
#define MAX_RX_PDU_SIZE            1460
#define MAX_TX_PDU_SIZE            1460 

#define ISCSI_MAX_ISCSI_NAME_LENGTH     128
#define ISCSI_MAX_CHAP_ID_SLENGTH       32
#define ISCSI_MAX_CHAP_ID_LENGTH       128
#define ISCSI_MAX_CHAP_PW_LENGTH        16

#define ISCSI_MAX_DHCP_VENDOR_ID_LENGTH 32
#define ISCSI_MAX_NUM_TARGETS      2
#define ISCSI_DEFAULT_HOST_NAME  "iscsiboot"

#endif /* __IPARMS_H__ */


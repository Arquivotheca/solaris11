/*$Header: /vobs/u320chim/src/chim/chimhdr/chimhw.h   /main/5   Mon Mar 17 18:18:35 2003   quan $*/


/***************************************************************************
*                                                                          *
* Copyright 1995-2003 Adaptec, Inc.  All Rights Reserved.                  *
*                                                                          *
* This software contains the valuable trade secrets of Adaptec or its      *
* licensors.  The software is protected under international copyright      *
* laws and treaties.  This software may only be used in accordance with    *
* terms of its accompanying license agreement.                             *
*                                                                          *
***************************************************************************/

/***************************************************************************
*
*  Module Name:   CHIMHW.H
*
*  Description:   Basic hardware
*
*  Owners:        ECX IC Firmware Team
*
*  Notes:         Contains very basic #defines:
*                    Endianness of hardware.
*                    Scatter-gather format
*                    Host bus (PCI, etc.)
*                 All HIM's compiled together must agree on
*                    the same chimhw.h
*
***************************************************************************/
#define HIM_HA_LITTLE_ENDIAN 1

#define HIM_SG_LIST_TYPE     0    /* phys addr, then length, with       */
                                  /*    end-of-list delimiter           */
                                  /* currently, no other types defined  */

#define HIM_HOST_BUS  HIM_HOST_BUS_PCI  /* possible types in chimcom.h  */


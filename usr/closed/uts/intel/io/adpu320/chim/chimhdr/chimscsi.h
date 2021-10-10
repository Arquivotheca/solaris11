/*$Header: /vobs/u320chim/src/chim/chimhdr/chimscsi.h   /main/5   Thu Jul 17 08:54:31 2003   rog22390 $*/

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
*  Module Name:   CHIMSCSI.H
*
*  Description:   SCSI-specific #defines
*
*  Owners:        Milpitas SCSI HIM Team
*
*  Notes:         Includes
*                     Entry point to get other function pointers
*                     Required size of InitTSCB
*                     HIM_SSA_INCLUDED for OSM convenience
*
***************************************************************************/
#ifndef HIM_CHIMSCSI_INCLUDED
#define HIM_CHIMSCSI_INCLUDED
#define HIM_ENTRYNAME_SCSI      SCSIGetFunctionPointers
#define HIM_SCSI_INIT_TSCB_SIZE 512
#endif





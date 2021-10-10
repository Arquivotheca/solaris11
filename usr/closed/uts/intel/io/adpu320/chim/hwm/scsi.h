/*$Header: /vobs/u320chim/src/chim/hwm/scsi.h   /main/3   Mon Mar 17 18:23:01 2003   quan $*/

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
*  Module Name:   SCSI.H
*
*  Description:   Shortcut front-end to include all other SCSI .h's.
*
*  Owners:        ECX IC Firmware Team
*
***************************************************************************/

typedef  struct SCSI_HIOB_ SCSI_HIOB;
typedef  struct SCSI_HHCB_ SCSI_HHCB;
typedef  struct SCSI_UNIT_CONTROL_ SCSI_UNIT_CONTROL;
typedef  struct SCSI_SCB_DESCRIPTOR_ SCSI_SCB_DESCRIPTOR;
typedef  struct SCSI_HW_INFORMATION_ SCSI_HW_INFORMATION;
/* SCSI_TARGET_OPERATION */
typedef  struct SCSI_NODE_ SCSI_NODE;
/* SCSI_TARGET_OPERATION */

#if !defined(SCSI_INTERNAL)
#include "chimhw.h"
#include "chimosm.h"
#include "chimcom.h"
#include "chimscsi.h"
#include "scsichim.h"
#include "scsiosm.h"
#else
typedef  union  SCSI_HOST_ADDRESS_ SCSI_HOST_ADDRESS;
typedef  struct  SCSI_BUFFER_DESCRIPTOR_ SCSI_BUFFER_DESCRIPTOR ;
#include "scsihw.h"
#include "scsiosd.h"
#include "scsicom.h"
#endif
      



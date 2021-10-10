/*$Header: /vobs/u320chim/src/chim/xlm/xlm.h   /main/3   Mon Mar 17 18:24:37 2003   quan $*/

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
*  Module Name:   XLM.H
*
*  Description:   Include files for translation management 
*
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         NONE
*
***************************************************************************/

#include "rsm.h"

#include "chimdef.h"
#include "xlmref.h"

/* CAUTION - these OEM1 #include's must be here. */
/* They need to be between xlmref.h and xlmdef.h */
#ifdef HIM_OEM1_INCLUDE_SCSI
#include "chimso1.h"
#include "chimdo1.h" 
#include "xlmoem1.h"
#endif 

#include "xlmdef.h"


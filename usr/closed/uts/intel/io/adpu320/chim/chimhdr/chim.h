/* $Header: /vobs/u320chim/src/chim/chimhdr/chim.h   /main/v366a/1   Wed May  8 19:44:58 2002   hu11135 $ */
/***************************************************************************
*                                                                          *
* Copyright 1995,1996,1997,1998,1999,2000,2001 Adaptec, Inc.,              *
* All Rights Reserved.                                                     *
*                                                                          *
* This software contains the valuable trade secrets of Adaptec.  The       *
* software is protected under copyright laws as an unpublished work of     *
* Adaptec.  Notice is for informational purposes only and does not imply   *
* publication.  The user of this software may make copies of the software  *
* for use with parts manufactured by Adaptec or under license from Adaptec *
* and for no other use.                                                    *
*                                                                          *
***************************************************************************/

/***************************************************************************
*
*  Module Name:   CHIM.H
*
*  Description:   Shortcut front-end to include all other CHIM .h's.
*
*  Owners:        ECX IC Firmware Team
*
***************************************************************************/
#ifndef  CHIM_INCLUDED
#define  CHIM_INCLUDED
#include "chimhw.h"
#include "chimosm.h"
#include "chimcom.h"

/* him-specific .h's - one per him */
#ifdef HIM_INCLUDE_SSA /* to be included by OSM writer in chimosm.h */
#include "chimssa.h"
#endif
#ifdef HIM_INCLUDE_SCSI
#include "chimscsi.h"
#endif
/* end of him-specific .h's */

#include "chimdef.h"

/* oem-specific .h's - this is for OEM1 */
#ifdef HIM_OEM1_INCLUDE_SCSI
#include "chimso1.h"
#include "chimdo1.h"
#endif

#endif /* #ifndef  CHIM_INCLUDED */



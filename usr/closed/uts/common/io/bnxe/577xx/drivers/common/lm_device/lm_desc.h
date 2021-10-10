/*******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2001-2005 Broadcom Corporation, ALL RIGHTS RESERVED.
 *
 *
 * Module Description:
 *
 *
 * History:
 *    11/15/01 havk             Inception.
 ******************************************************************************/

#ifndef _LM_DESC_H
#define _LM_DESC_H

#include "fw_defs.h"

#ifndef StringIt
#define _StringIt(x)                    #x
#define StringIt(x)                     _StringIt(x)
#endif

#if DBG
#define LM_DEBUG_STR "\r\nDEBUG version"
#else
#define LM_DEBUG_STR ""
#endif

#define LM_DRIVER_MAJOR_VER     6
#define LM_DRIVER_MINOR_VER     4
#define LM_DRIVER_FIX_NUM       43
#define LM_DRIVER_ENG_NUM       0

/* major product release version which corresponds to T2.8, T3.0, etc. */
#define LM_PRODUCT_MAJOR_VER    16
#define LM_PRODUCT_MINOR_VER    0
#define LM_PRODUCT_FIX_NUM      0

#define LM_COMPANY_NAME_STR     "Broadcom Corporation"
#define LM_COPYRIGHT_STR        "(c) COPYRIGHT 2001-2011 Broadcom Corporation"
#define LM_PRODUCT_NAME_STR     "Broadcom NetXtreme II 10GigE"

#define LM_INFO_STR             "\r\nFW Ver:" StringIt(BCM_5710_FW_MAJOR_VERSION) "." StringIt(BCM_5710_FW_MINOR_VERSION) "." StringIt(BCM_5710_FW_REVISION_VERSION) "." StringIt(BCM_5710_FW_ENGINEERING_VERSION) "\r\nFW Compile:" StringIt(BCM_5710_FW_COMPILE_FLAGS) LM_DEBUG_STR

#endif /* _LM_DESC_H */

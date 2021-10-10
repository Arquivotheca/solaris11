
/*
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2008-2011 Broadcom Corporation, ALL RIGHTS RESERVED.
 */

/* This file is included by lmdev/include/debug.h */

#ifndef __BNXE_DEBUG_H__
#define __BNXE_DEBUG_H__

#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/varargs.h>
#undef u /* see bnxe.h for explanation */

extern char * BnxeDevName(void *);


#ifdef DBG

/********************************************/
/* all DbgXXX() routines are used by the LM */
/********************************************/

#undef __FILE_STRIPPED__
#define __FILE_STRIPPED__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

void DbgMessageFunc(void * pDev,
                    int    level,
                    char * pFmt,
                    ...);

#define DbgMessageXX(_c, _m, _s, ...)                                  \
    DbgMessageFunc(_c, _m, "%s %p <%d> %s(%4d): " _s,                  \
                   BnxeDevName((void *)_c), (void *)_c,                \
                   (_m & LV_MASK), __FILE__, __LINE__, ## __VA_ARGS__)

#define DbgMessage  DbgMessageXX
#define DbgMessage1 DbgMessageXX
#define DbgMessage2 DbgMessageXX
#define DbgMessage3 DbgMessageXX
#define DbgMessage4 DbgMessageXX
#define DbgMessage5 DbgMessageXX
#define DbgMessage6 DbgMessageXX
#define DbgMessage7 DbgMessageXX

#define DbgBreak() cmn_err(CE_PANIC, "<%d> %s(%4d): DbgBreak!", \
                           ((FATAL) & LV_MASK),                 \
                           __FILE_STRIPPED__,                   \
                           __LINE__)

#define DbgBreakMsg(_s) cmn_err(CE_PANIC, "<%d> %s(%4d): " _s, \
                                ((FATAL) & LV_MASK),           \
                                __FILE_STRIPPED__,             \
                                __LINE__)

#define DbgBreakIf(_cond)                                                    \
    if (_cond)                                                               \
    {                                                                        \
        cmn_err(CE_PANIC, "<%d> %s(%4d): Condition Failed! - if ("#_cond")", \
                ((FATAL) & LV_MASK),                                         \
                __FILE_STRIPPED__,                                           \
                __LINE__);                                                   \
    }

#define DbgBreakFastPath()      DbgBreak()
#define DbgBreakMsgFastPath(_s) DbgBreakMsg(_s)
#define DbgBreakIfFastPath(_c)  DbgBreakIf(_c)

#define dbg_out(_c, _m, _s, _d1) DbgMessageXX(_c, _m, _s, _d1)

#endif /* DBG */


/*****************************************************************/
/* all BnxeDbgXXX() and BnxeLogXXX() routines are used by the UM */
/*****************************************************************/

#define BnxeDbgBreak(_c) cmn_err(CE_PANIC, "%s: <%d> %s(%4d): DbgBreak!", \
                                 BnxeDevName(_c),                         \
                                 ((FATAL) & LV_MASK),                     \
                                 __FILE_STRIPPED__,                       \
                                 __LINE__)

#define BnxeDbgBreakMsg(_c, _s) cmn_err(CE_PANIC, "%s: <%d> %s(%4d): " _s, \
                                        BnxeDevName(_c),                   \
                                        ((FATAL) & LV_MASK),               \
                                        __FILE_STRIPPED__,                 \
                                        __LINE__)

#define BnxeDbgBreakIf(_c, _cond)                                                \
    if (_cond)                                                                   \
    {                                                                            \
        cmn_err(CE_PANIC, "%s: <%d> %s(%4d): Condition Failed! - if ("#_cond")", \
                BnxeDevName(_c),                                                 \
                ((FATAL) & LV_MASK),                                             \
                __FILE_STRIPPED__,                                               \
                __LINE__);                                                       \
    }

#define BnxeDbgBreakFastPath(_c)           BnxeDbgBreak(_c)
#define BnxeDbgBreakMsgFastPath(_c, _s)    BnxeDbgBreakMsg(_c, _s)
#define BnxeDbgBreakIfFastPath(_c, _cond)  BnxeDbgBreakIf(_c, _cond)

void BnxeLogInfo(void * pDev, char * pFmt, ...);
void BnxeLogWarn(void * pDev, char * pFmt, ...);
/* for CE_PANIC use one of the BnxeDbgBreak macros above */

#ifdef DEBUG
void BnxeLogDbg(void * pDev, char * pFmt, ...);
#else
#define BnxeLogDbg
#endif

#endif /* __BNXE_DEBUG_H__ */


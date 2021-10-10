
/*
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2008-2011 Broadcom Corporation, ALL RIGHTS RESERVED.
 */

#include "bnxe.h"

#define BNXE_LOG_LEN 256


#ifdef DBG

void DbgMessageFunc(void * pDev,
                    int    level,
                    char * pFmt,
                    ...)
{
    va_list argp;
    int ce;

    if (!LOG_MSG(level))
    {
        return;
    }

    ce = ((((level) & LV_VERBOSE) == LV_VERBOSE) ? CE_NOTE :
          (((level) & LV_INFORM) == LV_INFORM)   ? CE_NOTE :
          (((level) & LV_WARN) == LV_WARN)       ? CE_WARN :
                                                   CE_PANIC);

    va_start(argp, pFmt);

    vcmn_err(ce,
             pFmt,
             argp);

    va_end(argp);
}

#endif /* DBG */


void BnxeLogInfo(void * pDev,
                 char * pFmt,
                 ...)
{
    um_device_t * pUM = (um_device_t *)pDev;
    char buf[BNXE_LOG_LEN];
    va_list argp;

    /*
     * Info message are logged to syslog only if logEnable is
     * turned on.  They are never logged to the console.  If
     * pUM is NULL then the log is allowed through as if logEnable
     * was turned on.
     */

    if (pUM && !pUM->devParams.logEnable)
    {
        return;
    }
    /* if !pUM then let the log through */

    va_start(argp, pFmt);
    vsnprintf(buf, sizeof(buf), pFmt, argp);
    va_end(argp);

    cmn_err(CE_NOTE, "!%s: %s", BnxeDevName(pUM), buf);
}


void BnxeLogWarn(void * pDev,
                 char * pFmt,
                 ...)
{
    um_device_t * pUM = (um_device_t *)pDev;
    char buf[BNXE_LOG_LEN];
    va_list argp;

    /*
     * Warning message are always logged to syslog.  They are
     * never logged to the console.
     */

    va_start(argp, pFmt);
    vsnprintf(buf, sizeof(buf), pFmt, argp);
    va_end(argp);

    cmn_err(CE_WARN, "!%s: %s", BnxeDevName(pUM), buf);
}


#ifdef DEBUG

void BnxeLogDbg(void * pDev,
                char * pFmt,
                ...)
{
    um_device_t * pUM = (um_device_t *)pDev;
    char buf[BNXE_LOG_LEN];
    va_list argp;

    /*
     * Debug message are always logged to both syslog and the
     * console.  Debug messages are only available when the
     * DBG compile time flag is turned on.
     */

    va_start(argp, pFmt);
    vsnprintf(buf, sizeof(buf), pFmt, argp);
    va_end(argp);

    cmn_err(CE_WARN, "%s: %s", BnxeDevName(pUM), buf);
}

#endif /* DEBUG */


void BnxeDumpMem(um_device_t * pUM,
                 char *        pTag,
                 u8_t *        pMem,
                 u32_t         len)
{
    char buf[256];
    char c[32];
    int  xx;

    mutex_enter(&bnxeLoaderMutex);

    cmn_err(CE_WARN, "%s ++++++++++++ %s", BnxeDevName(pUM), pTag);
    strcpy(buf, "** 000: ");

    for (xx = 0; xx < len; xx++)
    {
        if ((xx != 0) && (xx % 16 == 0))
        {
            cmn_err(CE_WARN, buf);
            strcpy(buf, "** ");
            snprintf(c, sizeof(c), "%03x", xx);
            strcat(buf, c);
            strcat(buf, ": ");
        }

        snprintf(c, sizeof(c), "%02x ", *pMem);
        strcat(buf, c);

        pMem++;
    }

    cmn_err(CE_WARN, buf);
    cmn_err(CE_WARN, "%s ------------ %s", BnxeDevName(pUM), pTag);

    mutex_exit(&bnxeLoaderMutex);
}


void BnxeDumpPkt(um_device_t * pUM,
                 char *        pTag,
                 mblk_t *      pMblk,
                 boolean_t     contents)
{
    char buf[256];
    char c[32];
    u8_t * pMem;
    int  i, xx = 0;

    mutex_enter(&bnxeLoaderMutex);

    cmn_err(CE_WARN, "%s ++++++++++++ %s", BnxeDevName(pUM), pTag);

    while (pMblk)
    {
        pMem = pMblk->b_rptr;
        strcpy(buf, "** > ");
        snprintf(c, sizeof(c), "%03x", xx);
        strcat(buf, c);
        strcat(buf, ": ");

        if (contents)
        {
            for (i = 0; i < MBLKL(pMblk); i++)
            {
                if ((xx != 0) && (xx % 16 == 0))
                {
                    cmn_err(CE_WARN, buf);
                    strcpy(buf, "**   ");
                    snprintf(c, sizeof(c), "%03x", xx);
                    strcat(buf, c);
                    strcat(buf, ": ");
                }

                snprintf(c, sizeof(c), "%02x ", *pMem);
                strcat(buf, c);

                pMem++;
                xx++;
            }
        }
        else
        {
            snprintf(c, sizeof(c), "%d", (int)MBLKL(pMblk));
            strcat(buf, c);
            xx += MBLKL(pMblk);
        }

        cmn_err(CE_WARN, buf);
        pMblk = pMblk->b_cont;
    }

    cmn_err(CE_WARN, "%s ------------ %s", BnxeDevName(pUM), pTag);

    mutex_exit(&bnxeLoaderMutex);
}


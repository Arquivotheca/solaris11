/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2007-2010 Broadcom Corporation, ALL RIGHTS RESERVED.
 ******************************************************************************/

#include "bnx.h"

typedef struct
{
    kstat_named_t version;
    kstat_named_t versionFW;
    kstat_named_t chipName;
    kstat_named_t intrAlloc;
    kstat_named_t intrFired;
    kstat_named_t intrInDisabled;
    kstat_named_t intrNoChange;
} BnxKstat;

#define BNX_KSTAT_SIZE (sizeof(BnxKstat) / sizeof(kstat_named_t))


static int BnxKstatUpdate(kstat_t * kstats,
                           int       rw)
{
    BnxKstat *    pStats = (BnxKstat *)kstats->ks_data;
    um_device_t * pUM    = (um_device_t *)kstats->ks_private;
    lm_device_t * pLM    = (lm_device_t *)pUM;

    if (rw == KSTAT_WRITE)
    {
        return EACCES;
    }

    mutex_enter(&pUM->kstatMutex);

    strncpy(pStats->version.value.c,   pUM->version,   sizeof(pStats->version.value.c));
    strncpy(pStats->versionFW.value.c, pUM->versionFW, sizeof(pStats->versionFW.value.c));
    strncpy(pStats->chipName.value.c,  pUM->chipName,  sizeof(pStats->chipName.value.c));
    strncpy(pStats->intrAlloc.value.c, pUM->intrAlloc, sizeof(pStats->intrAlloc.value.c));
    pStats->intrFired.value.ui64      = pUM->intr_count;
    pStats->intrInDisabled.value.ui64 = pUM->intr_in_disabled;
    pStats->intrNoChange.value.ui64   = pUM->intr_no_change;

    mutex_exit(&pUM->kstatMutex);

    return 0;
}


boolean_t BnxKstatInit(um_device_t * pUM)
{
    lm_device_t * pLM = (lm_device_t *)pUM;
    char buf[32];
    int idx;

    BnxKstat *    pStats;
#define BNX_KSTAT(f, t)     kstat_named_init(&pStats->f, #f, t)

    if ((pUM->kstats = kstat_create("bnx",
                                    pUM->instance,
                                    "statistics",
                                    "net",
                                    KSTAT_TYPE_NAMED,
                                    BNX_KSTAT_SIZE,
                                    0)) == NULL)
    {
        cmn_err(CE_WARN, "%s: Failed to create kstat", pUM->dev_name);
        return B_FALSE;
    }

    pStats = (BnxKstat *)pUM->kstats->ks_data;

    BNX_KSTAT(version,             KSTAT_DATA_CHAR);
    BNX_KSTAT(versionFW,           KSTAT_DATA_CHAR);
    BNX_KSTAT(chipName,            KSTAT_DATA_CHAR);
    BNX_KSTAT(intrAlloc,           KSTAT_DATA_CHAR);
    BNX_KSTAT(intrFired,           KSTAT_DATA_UINT64);
    BNX_KSTAT(intrInDisabled,      KSTAT_DATA_UINT64);
    BNX_KSTAT(intrNoChange,        KSTAT_DATA_UINT64);

    pUM->kstats->ks_update  = BnxKstatUpdate;
    pUM->kstats->ks_private = (void *)pUM;

    mutex_init(&pUM->kstatMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));

    kstat_install(pUM->kstats);

    return B_TRUE;
}


void BnxKstatFini(um_device_t * pUM)
{
    if (pUM->kstats)
    {
        kstat_delete(pUM->kstats);
        pUM->kstats = NULL;
    }

    mutex_destroy(&pUM->kstatMutex);
}


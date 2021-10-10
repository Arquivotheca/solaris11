
/*
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2008-2011 Broadcom Corporation, ALL RIGHTS RESERVED.
 */

/*
 * Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "bnxe.h"

#define BNXE_TIMER_INTERVAL 1000000 /* usecs (once a second for stats) */


static void BnxeTimer(void * pArg)
{
    um_device_t * pUM = (um_device_t *)pArg;
    lm_device_t * pLM = &pUM->lm_dev;

    BNXE_LOCK_ENTER_TIMER(pUM);

    if (pUM->timerEnabled != B_TRUE)
    {
        BNXE_LOCK_EXIT_TIMER(pUM);
        return;
    }

    lm_stats_on_timer(pLM);

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pLM->vars.reg_handle[BAR_0]) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_UNAFFECTED);
    }

    pUM->timerID = timeout(BnxeTimer, (void *)pUM,
                           drv_usectohz(BNXE_TIMER_INTERVAL));

    BNXE_LOCK_EXIT_TIMER(pUM);
}


void BnxeTimerStart(um_device_t * pUM)
{
    atomic_swap_32(&pUM->timerEnabled, B_TRUE);

    pUM->lm_dev.vars.stats.stats_collect.timer_wakeup = 0; /* reset */

    pUM->timerID = timeout(BnxeTimer, (void *)pUM,
                           drv_usectohz(BNXE_TIMER_INTERVAL));
}


void BnxeTimerStop(um_device_t * pUM)
{
    atomic_swap_32(&pUM->timerEnabled, B_FALSE);

    BNXE_LOCK_ENTER_TIMER(pUM);
    BNXE_LOCK_EXIT_TIMER(pUM);

    untimeout(pUM->timerID);
    pUM->timerID = 0;
}



/*
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2008-2011 Broadcom Corporation, ALL RIGHTS RESERVED.
 */

#include "bnxe.h"


typedef struct _BnxeWorkItem
{
    s_list_entry_t link;
    void *         pWorkData;
    u32_t          workDataLen;
    u32_t          delayMs;
    void (*pWorkCbkCopy)(um_device_t *, void *, u32_t);
    void (*pWorkCbkNoCopy)(um_device_t *, void *);
    void (*pWorkCbkGeneric)(um_device_t *);
} BnxeWorkItem;


static void BnxeWorkQueueInstanceWaitAndDestroy(BnxeWorkQueueInstance * pWorkq)
{
    ddi_taskq_wait(pWorkq->pTaskq);
    ddi_taskq_destroy(pWorkq->pTaskq);
    pWorkq->pTaskq = NULL;
    memset(pWorkq->taskqName, 0, BNXE_STR_SIZE);
    mutex_destroy(&pWorkq->workQueueMutex);
}


boolean_t BnxeWorkQueueInit(um_device_t * pUM)
{
    pUM->workqs.instq.pUM = pUM;

    strcpy(pUM->workqs.instq.taskqName, pUM->devName);
    strcat(pUM->workqs.instq.taskqName, "_inst_q");

    mutex_init(&pUM->workqs.instq.workQueueMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));

    if ((pUM->workqs.instq.pTaskq =
         ddi_taskq_create(pUM->pDev,
                          pUM->workqs.instq.taskqName,
                          1,
                          TASKQ_DEFAULTPRI,
                          0)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to create the workqs instq");
        return B_FALSE;
    }

    pUM->workqs.instq.pUM = pUM;

    strcpy(pUM->workqs.delayq.taskqName, pUM->devName);
    strcat(pUM->workqs.delayq.taskqName, "_delay_q");

    mutex_init(&pUM->workqs.delayq.workQueueMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));

    if ((pUM->workqs.delayq.pTaskq =
         ddi_taskq_create(pUM->pDev,
                          pUM->workqs.delayq.taskqName,
                          16, /* XXX Is this enough? */
                          TASKQ_DEFAULTPRI,
                          0)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to create the workqs delayq");
        BnxeWorkQueueInstanceWaitAndDestroy(&pUM->workqs.instq);
        return B_FALSE;
    }

    pUM->workqs.delayq.pUM = pUM;

    return B_TRUE;
}


void BnxeWorkQueueWaitAndDestroy(um_device_t * pUM)
{
    BnxeWorkQueueInstanceWaitAndDestroy(&pUM->workqs.instq);
    BnxeWorkQueueInstanceWaitAndDestroy(&pUM->workqs.delayq);
}


static void BnxeWorkQueueDispatch(void * pArg)
{
    BnxeWorkQueueInstance * pWorkq = (BnxeWorkQueueInstance *)pArg;
    um_device_t * pUM = (um_device_t *)pWorkq->pUM;
    BnxeWorkItem * pWorkItem;

    mutex_enter(&pWorkq->workQueueMutex);
    pWorkItem = (BnxeWorkItem *)s_list_pop_head(&pWorkq->workQueue);
    mutex_exit(&pWorkq->workQueueMutex);

    if (pWorkItem == NULL)
    {
        BnxeLogWarn(pUM, "Work item is NULL!");
        pWorkq->workItemError++;
        return;
    }

    if ((pWorkItem->pWorkCbkCopy == NULL) &&
        (pWorkItem->pWorkCbkNoCopy == NULL) &&
        (pWorkItem->pWorkCbkGeneric == NULL))
    {
        BnxeLogWarn(pUM, "Work item callback is NULL!");
        pWorkq->workItemError++;
        goto BnxeWorkQueueDispatch_done;
    }

    if (pWorkItem->delayMs > 0)
    {
        /* this only occurs when processing the delayq */
        drv_usecwait(pWorkItem->delayMs * 1000);
    }

    if (pWorkItem->pWorkCbkCopy)
    {
        pWorkItem->pWorkCbkCopy(pUM,
                                pWorkItem->pWorkData,
                                pWorkItem->workDataLen);
    }
    else if (pWorkItem->pWorkCbkNoCopy)
    {
        pWorkItem->pWorkCbkNoCopy(pUM,
                                  pWorkItem->pWorkData);
    }
    else /* (pWorkItem->pWorkCbkGeneric) */
    {
        pWorkItem->pWorkCbkGeneric(pUM);
    }

    pWorkq->workItemComplete++;

BnxeWorkQueueDispatch_done:

    kmem_free(pWorkItem, (sizeof(BnxeWorkItem) + pWorkItem->workDataLen));
}


boolean_t BnxeWorkQueueAdd(um_device_t * pUM,
                           void (*pWorkCbkCopy)(um_device_t *, void *, u32_t),
                           void * pWorkData,
                           u32_t  workDataLen)
{
    BnxeWorkItem * pWorkItem;

    if ((pWorkItem = kmem_zalloc((sizeof(BnxeWorkItem) + workDataLen),
                                 KM_NOSLEEP)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to allocate memory for work item!");
        return B_FALSE;
    }

    pWorkItem->pWorkData       = (pWorkItem + 1);
    pWorkItem->workDataLen     = workDataLen;
    pWorkItem->pWorkCbkCopy    = pWorkCbkCopy;
    pWorkItem->pWorkCbkNoCopy  = NULL;
    pWorkItem->pWorkCbkGeneric = NULL;
    pWorkItem->delayMs         = 0;

    memcpy(pWorkItem->pWorkData, pWorkData, workDataLen);

    mutex_enter(&pUM->workqs.instq.workQueueMutex);
    s_list_push_tail(&pUM->workqs.instq.workQueue, &pWorkItem->link);
    mutex_exit(&pUM->workqs.instq.workQueueMutex);

    pUM->workqs.instq.workItemQueued++;

    ddi_taskq_dispatch(pUM->workqs.instq.pTaskq,
                       BnxeWorkQueueDispatch,
                       (void *)&pUM->workqs.instq,
                       DDI_NOSLEEP);

    return B_TRUE;
}


boolean_t BnxeWorkQueueAddNoCopy(um_device_t * pUM,
                                 void (*pWorkCbkNoCopy)(um_device_t *, void *),
                                 void * pWorkData)
{
    BnxeWorkItem * pWorkItem;

    if ((pWorkItem = kmem_zalloc(sizeof(BnxeWorkItem), KM_NOSLEEP)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to allocate memory for work item!");
        return B_FALSE;
    }

    pWorkItem->pWorkData       = pWorkData;
    pWorkItem->workDataLen     = 0;
    pWorkItem->pWorkCbkCopy    = NULL;
    pWorkItem->pWorkCbkNoCopy  = pWorkCbkNoCopy;
    pWorkItem->pWorkCbkGeneric = NULL;
    pWorkItem->delayMs         = 0;

    mutex_enter(&pUM->workqs.instq.workQueueMutex);
    s_list_push_tail(&pUM->workqs.instq.workQueue, &pWorkItem->link);
    mutex_exit(&pUM->workqs.instq.workQueueMutex);

    pUM->workqs.instq.workItemQueued++;

    ddi_taskq_dispatch(pUM->workqs.instq.pTaskq,
                       BnxeWorkQueueDispatch,
                       (void *)&pUM->workqs.instq,
                       DDI_NOSLEEP);

    return B_TRUE;
}


boolean_t BnxeWorkQueueAddGeneric(um_device_t * pUM,
                                  void (*pWorkCbkGeneric)(um_device_t *))
{
    BnxeWorkItem * pWorkItem;

    if ((pWorkItem = kmem_zalloc(sizeof(BnxeWorkItem), KM_NOSLEEP)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to allocate memory for work item!");
        return B_FALSE;
    }

    pWorkItem->pWorkData       = NULL;
    pWorkItem->workDataLen     = 0;
    pWorkItem->pWorkCbkCopy    = NULL;
    pWorkItem->pWorkCbkNoCopy  = NULL;
    pWorkItem->pWorkCbkGeneric = pWorkCbkGeneric;
    pWorkItem->delayMs         = 0;

    mutex_enter(&pUM->workqs.instq.workQueueMutex);
    s_list_push_tail(&pUM->workqs.instq.workQueue, &pWorkItem->link);
    mutex_exit(&pUM->workqs.instq.workQueueMutex);

    pUM->workqs.instq.workItemQueued++;

    ddi_taskq_dispatch(pUM->workqs.instq.pTaskq,
                       BnxeWorkQueueDispatch,
                       (void *)&pUM->workqs.instq,
                       DDI_NOSLEEP);

    return B_TRUE;
}


boolean_t BnxeWorkQueueAddDelay(um_device_t * pUM,
                                void (*pWorkCbkCopy)(um_device_t *, void *, u32_t),
                                void * pWorkData,
                                u32_t  workDataLen,
                                u32_t  delayMs)
{
    BnxeWorkItem * pWorkItem;

    if ((pWorkItem = kmem_zalloc((sizeof(BnxeWorkItem) + workDataLen),
                                 KM_NOSLEEP)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to allocate memory for work item!");
        return B_FALSE;
    }

    pWorkItem->pWorkData       = (pWorkItem + 1);
    pWorkItem->workDataLen     = workDataLen;
    pWorkItem->pWorkCbkCopy    = pWorkCbkCopy;
    pWorkItem->pWorkCbkNoCopy  = NULL;
    pWorkItem->pWorkCbkGeneric = NULL;
    pWorkItem->delayMs         = delayMs;

    memcpy(pWorkItem->pWorkData, pWorkData, workDataLen);

    mutex_enter(&pUM->workqs.delayq.workQueueMutex);
    s_list_push_tail(&pUM->workqs.delayq.workQueue, &pWorkItem->link);
    mutex_exit(&pUM->workqs.delayq.workQueueMutex);

    pUM->workqs.delayq.workItemQueued++;

    ddi_taskq_dispatch(pUM->workqs.delayq.pTaskq,
                       BnxeWorkQueueDispatch,
                       (void *)&pUM->workqs.delayq,
                       DDI_NOSLEEP);

    return B_TRUE;
}


boolean_t BnxeWorkQueueAddDelayNoCopy(um_device_t * pUM,
                                      void (*pWorkCbkNoCopy)(um_device_t *, void *),
                                      void * pWorkData,
                                      u32_t  delayMs)
{
    BnxeWorkItem * pWorkItem;

    if ((pWorkItem = kmem_zalloc(sizeof(BnxeWorkItem), KM_NOSLEEP)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to allocate memory for work item!");
        return B_FALSE;
    }

    pWorkItem->pWorkData       = pWorkData;
    pWorkItem->workDataLen     = 0;
    pWorkItem->pWorkCbkCopy    = NULL;
    pWorkItem->pWorkCbkNoCopy  = pWorkCbkNoCopy;
    pWorkItem->pWorkCbkGeneric = NULL;
    pWorkItem->delayMs         = delayMs;

    mutex_enter(&pUM->workqs.delayq.workQueueMutex);
    s_list_push_tail(&pUM->workqs.delayq.workQueue, &pWorkItem->link);
    mutex_exit(&pUM->workqs.delayq.workQueueMutex);

    pUM->workqs.delayq.workItemQueued++;

    ddi_taskq_dispatch(pUM->workqs.delayq.pTaskq,
                       BnxeWorkQueueDispatch,
                       (void *)&pUM->workqs.delayq,
                       DDI_NOSLEEP);

    return B_TRUE;
}


boolean_t BnxeWorkQueueAddDelayGeneric(um_device_t * pUM,
                                       void (*pWorkCbkGeneric)(um_device_t *),
                                       u32_t delayMs)
{
    BnxeWorkItem * pWorkItem;

    if ((pWorkItem = kmem_zalloc(sizeof(BnxeWorkItem), KM_NOSLEEP)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to allocate memory for work item!");
        return B_FALSE;
    }

    pWorkItem->pWorkData       = NULL;
    pWorkItem->workDataLen     = 0;
    pWorkItem->pWorkCbkCopy    = NULL;
    pWorkItem->pWorkCbkNoCopy  = NULL;
    pWorkItem->pWorkCbkGeneric = pWorkCbkGeneric;
    pWorkItem->delayMs         = delayMs;

    mutex_enter(&pUM->workqs.delayq.workQueueMutex);
    s_list_push_tail(&pUM->workqs.delayq.workQueue, &pWorkItem->link);
    mutex_exit(&pUM->workqs.delayq.workQueueMutex);

    pUM->workqs.delayq.workItemQueued++;

    ddi_taskq_dispatch(pUM->workqs.delayq.pTaskq,
                       BnxeWorkQueueDispatch,
                       (void *)&pUM->workqs.delayq,
                       DDI_NOSLEEP);

    return B_TRUE;
}


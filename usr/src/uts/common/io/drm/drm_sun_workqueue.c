/* BEGIN CSTYLED */

/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/sunddi.h>
#include <sys/types.h>

#include "drm_sun_workqueue.h"

int
queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	return (ddi_taskq_dispatch(wq->taskq, work->func, work, DDI_SLEEP));
}

void
init_work(struct work_struct *work, void (*func)(void *))
{
	work->func = func;
}

struct workqueue_struct *
create_workqueue(dev_info_t *dip, char *name)
{
	struct workqueue_struct *wq;

	wq = kmem_zalloc(sizeof (struct workqueue_struct), KM_SLEEP);
	wq->taskq = ddi_taskq_create(dip, name, 1, TASKQ_DEFAULTPRI, 0);
	if (wq->taskq == NULL)
		goto fail;
	wq->name = name;

	return wq;

fail :
	kmem_free(wq, sizeof (struct workqueue_struct));
	return (NULL);
}

void
destroy_workqueue(struct workqueue_struct *wq)
{
	if (wq) {
		ddi_taskq_destroy(wq->taskq);
		kmem_free(wq, sizeof (struct workqueue_struct));
	}
}


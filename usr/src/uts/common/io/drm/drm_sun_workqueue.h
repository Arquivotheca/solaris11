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

#ifndef __DRM_SUN_WORKQUEUE_H__
#define __DRM_SUN_WORKQUEUE_H__

typedef void (* taskq_func_t)(void *);

#define INIT_WORK(work, func) \
	init_work((work), ((taskq_func_t)(func)))

struct work_struct {
	void (*func) (void *);
};

struct workqueue_struct {
	ddi_taskq_t *taskq;
	char *name;
};

extern int queue_work(struct workqueue_struct *wq, struct work_struct *work);
extern void init_work(struct work_struct *work, void (*func)(void *));
extern struct workqueue_struct *create_workqueue(dev_info_t *dip, char *name);
extern void destroy_workqueue(struct workqueue_struct *wq);

#endif /* __DRM_SUN_WORKQUEUE_H__ */

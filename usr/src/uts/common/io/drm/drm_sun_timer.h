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

#ifndef __DRM_TIMER_H__
#define __DRM_TIMER_H__

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ksynch.h>
#include "drm_linux_list.h"

#define del_timer_sync del_timer

struct timer_list {
	struct list_head *head;
	void (*func)(void *);
	void *arg;
	clock_t expires;
	timeout_id_t timer_id;
	kmutex_t lock;
};

extern void init_timer(struct timer_list *timer);
extern void destroy_timer(struct timer_list *timer);
extern void setup_timer(struct timer_list *timer, void (*func)(void *), void *arg);
extern void mod_timer(struct timer_list *timer, clock_t expires);
extern void del_timer(struct timer_list *timer);

#endif /* __DRM_TIMER_H__ */

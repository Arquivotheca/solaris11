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

#include "drm_sun_timer.h"

void
init_timer(struct timer_list *timer)
{
	mutex_init(&timer->lock, NULL, MUTEX_DRIVER, NULL);
}

void
destroy_timer(struct timer_list *timer)
{
	mutex_destroy(&timer->lock);
}

void
setup_timer(struct timer_list *timer, void (*func)(void *), void *arg)
{
	timer->func = func;
	timer->arg = arg;
}

void
mod_timer(struct timer_list *timer, clock_t expires)
{
	mutex_enter(&timer->lock);
	(void) untimeout(timer->timer_id);
	timer->expires = expires;
	timer->timer_id = timeout(timer->func, timer->arg, timer->expires);
	mutex_exit(&timer->lock);
}

void
del_timer(struct timer_list *timer)
{
	(void) untimeout(timer->timer_id);
}


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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Simple implementation of timeout functionality. The granuality is a second.
 *
 * Unfortunately, we cannot use libinetutil's 'tq' (timer queues) implementation
 * because they aren't MT-safe. Also, 'tq' by itself does not provide an event
 * driven model and we have to use libinetutil's 'eh' (event handlers) and this
 * again isn't MT-safe.
 *
 * Achieving MT-safeness outside 'tq' or 'eh', i.e. from here as wrappers will
 * not work as well, because the 'tq' will end up calling the timeout function,
 * holding the synchronization lock and if the called function tries to
 * reschedule itself on 'tq' then we will end up in a deadlock.
 */

#include <pthread.h>
#include <stdlib.h>
#include <sys/list.h>
#include "lldp_impl.h"

typedef struct timeout {
	list_node_t	lldp_to_node;
	hrtime_t	lldp_to_val;
	uint32_t	lldp_to_id;
	void 		(*lldp_to_cbfunc)(void *);
	void 		*lldp_to_cbarg;
} lldp_timeout_t;

static pthread_mutex_t lldp_to_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  lldp_to_cv = PTHREAD_COND_INITIALIZER;
static list_t lldp_to_list;
static list_t lldp_cur_to_start;

/*
 * LLDP_LONG_SLEEP_TIME = (24 * 60 * 60 * NANOSEC)
 */
#define	LLDP_LONG_SLEEP_TIME	(0x15180LL * 0x3B9ACA00LL)

uint32_t lldp_timer_id = 0;

/*
 * Invoke the callback function
 */
/* ARGSUSED */
static void *
lldp_run_to_functions(void *arg)
{
	lldp_timeout_t *to = NULL;

	lldp_mutex_lock(&lldp_to_mutex);
	while ((to = list_head(&lldp_cur_to_start)) != NULL) {
		list_remove(&lldp_cur_to_start, to);
		lldp_mutex_unlock(&lldp_to_mutex);
		to->lldp_to_cbfunc(to->lldp_to_cbarg);
		free(to);
		lldp_mutex_lock(&lldp_to_mutex);
	}
	lldp_mutex_unlock(&lldp_to_mutex);
	pthread_exit(NULL);
	return ((void *)0);
}

/*
 * In the very very unlikely case timer id wraps around and we have two timers
 * with the same id. If that happens timer with the least amount of time left
 * will be deleted. In case both timers have same time left than the one that
 * was scheduled first will be deleted as it will be in the front of the list.
 */
boolean_t
lldp_untimeout(uint32_t id)
{
	boolean_t	ret = B_FALSE;
	lldp_timeout_t	*cur_to;

	lldp_mutex_lock(&lldp_to_mutex);

	/*
	 * Check if this is in the to-be run list
	 */
	if ((cur_to = list_head(&lldp_cur_to_start)) != NULL) {
		while (cur_to != NULL) {
			if (cur_to->lldp_to_id == id) {
				list_remove(&lldp_cur_to_start, cur_to);
				free(cur_to);
				ret = B_TRUE;
				break;
			}
			cur_to = list_next(&lldp_cur_to_start, cur_to);
		}
	}

	/*
	 * Check if this is in the to-be scheduled list
	 */
	if (!ret && (cur_to = list_head(&lldp_to_list)) != NULL) {
		while (cur_to != NULL) {
			if (cur_to->lldp_to_id == id) {
				list_remove(&lldp_to_list, cur_to);
				free(cur_to);
				ret = B_TRUE;
				break;
			}
			cur_to = list_next(&lldp_to_list, cur_to);
		}
	}
	lldp_mutex_unlock(&lldp_to_mutex);
	return (ret);
}

/*
 * Add a new timeout
 */
uint32_t
lldp_timeout(void *arg, void (*callback_func)(void *),
    struct timeval *timeout_time)
{
	lldp_timeout_t	*new_to;
	lldp_timeout_t	*cur_to;
	hrtime_t	future_time;
	uint32_t	tid;

	new_to = malloc(sizeof (lldp_timeout_t));
	if (new_to == NULL)
		return (0);

	future_time = ((hrtime_t)timeout_time->tv_sec * (hrtime_t)NANOSEC) +
	    ((hrtime_t)(timeout_time->tv_usec * MILLISEC)) + gethrtime();
	if (future_time <= 0L) {
		free(new_to);
		return (0);
	}

	new_to->lldp_to_val = future_time;
	new_to->lldp_to_cbfunc = callback_func;
	new_to->lldp_to_cbarg = arg;

	lldp_mutex_lock(&lldp_to_mutex);
	lldp_timer_id++;
	if (lldp_timer_id == 0)
		lldp_timer_id++;
	tid = lldp_timer_id;
	new_to->lldp_to_id = tid;

	cur_to = list_head(&lldp_to_list);
	while (cur_to != NULL) {
		if (new_to->lldp_to_val < cur_to->lldp_to_val) {
			list_insert_before(&lldp_to_list, cur_to, new_to);
			break;
		}
		cur_to = list_next(&lldp_to_list, cur_to);
	}
	if (cur_to == NULL)
		list_insert_tail(&lldp_to_list, new_to);
	(void) pthread_cond_signal(&lldp_to_cv);
	lldp_mutex_unlock(&lldp_to_mutex);
	return (tid);
}

/*
 * Schedule the next timeout
 */
static hrtime_t
lldp_schedule_to_functions()
{
	lldp_timeout_t	*to = NULL;
	lldp_timeout_t	*last_to = NULL;
	boolean_t	create_thread = B_FALSE;
	hrtime_t	current_time;

	/*
	 * Thread is holding the mutex.
	 */
	current_time = gethrtime();
	if ((to = list_head(&lldp_to_list)) == NULL)
		return ((hrtime_t)LLDP_LONG_SLEEP_TIME + current_time);

	/*
	 * Get all the tos that have fired.
	 */
	while (to != NULL && to->lldp_to_val <= current_time) {
		last_to = to;
		to = list_next(&lldp_to_list, to);
	}

	if (last_to != NULL) {
		to = list_head(&lldp_to_list);
		if (list_head(&lldp_cur_to_start) == NULL)
			create_thread = B_TRUE;
		while (to != NULL) {
			list_remove(&lldp_to_list, to);
			list_insert_tail(&lldp_cur_to_start, to);
			if (to == last_to)
				break;
			to = list_head(&lldp_to_list);
		}
		if (create_thread) {
			pthread_t	thr;

			(void) pthread_create(&thr, NULL, lldp_run_to_functions,
			    NULL);
			(void) pthread_detach(thr);
		}
	}
	if ((to = list_head(&lldp_to_list)) != NULL)
		return (to->lldp_to_val);
	else
		return ((hrtime_t)LLDP_LONG_SLEEP_TIME + current_time);
}

/*
 * The timer routine
 */
/* ARGSUSED */
static void *
lldp_timer_thr(void *arg)
{
	timestruc_t	to;
	hrtime_t	current_time;
	hrtime_t	next_to_time;
	hrtime_t	delta;
	struct timeval tim;

	lldp_mutex_lock(&lldp_to_mutex);
	next_to_time =  lldp_schedule_to_functions();
	current_time = gethrtime();
	delta = next_to_time - current_time;
	for (;;) {
		(void) gettimeofday(&tim, NULL);
		to.tv_sec = tim.tv_sec + (delta / NANOSEC);
		to.tv_nsec = (hrtime_t)(tim.tv_usec * MILLISEC) +
		    (delta % NANOSEC);
		if (to.tv_nsec > NANOSEC) {
			to.tv_sec += (to.tv_nsec / NANOSEC);
			to.tv_nsec %= NANOSEC;
		}
		(void) pthread_cond_timedwait(&lldp_to_cv,
		    &lldp_to_mutex, &to);
		/*
		 * We return from timedwait because we either timed out
		 * or a new element was added and we need to reset the time
		 */
again:
		next_to_time =  lldp_schedule_to_functions();
		current_time = gethrtime();
		delta = next_to_time - current_time;
		if (delta <= 0)
			goto again;
	}
	/* NOTREACHED */
	lldp_mutex_unlock(&lldp_to_mutex);
	return ((void *)0);
}

/*
 * The init routine, starts the timer thread
 */
int
lldp_timeout_init()
{
	pthread_attr_t	attr;
	int		err;

	(void) pthread_attr_init(&attr);
	(void) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	err = pthread_create(NULL, &attr, lldp_timer_thr, NULL);
	(void) pthread_attr_destroy(&attr);
	if (err == 0) {
		list_create(&lldp_to_list, sizeof (lldp_timeout_t),
		    offsetof(lldp_timeout_t, lldp_to_node));
		list_create(&lldp_cur_to_start, sizeof (lldp_timeout_t),
		    offsetof(lldp_timeout_t, lldp_to_node));
	}
	return (err);
}

/*
 * Given a timerID, it returns the number of seconds after which the
 * timer will fire.
 */
int
lldp_timerid2time(uint32_t id, uint16_t *time)
{
	lldp_timeout_t	*current;
	int		err = ENOENT;

	lldp_mutex_lock(&lldp_to_mutex);
	/* check if this is in the to-be run list */
	current = list_head(&lldp_cur_to_start);
	while (current != NULL && current->lldp_to_id != id)
		current = list_next(&lldp_cur_to_start, current);
	if (current == NULL) {
		/* check if this is in the to-be scheduled list */
		current = list_head(&lldp_to_list);
		while (current != NULL && current->lldp_to_id != id)
			current = list_next(&lldp_to_list, current);
	}
	if (current != NULL) {
		*time = (current->lldp_to_val - gethrtime()) / NANOSEC;
		err = 0;
	}
	lldp_mutex_unlock(&lldp_to_mutex);
	return (err);
}

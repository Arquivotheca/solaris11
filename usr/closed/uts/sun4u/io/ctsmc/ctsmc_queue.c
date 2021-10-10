/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * queue (FIFO) routines - System Management Controller Driver
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/ctsmc_queue.h>

/*
 * Initialize a queue with a given size and initial size
 * 'startval' determines which values can be enqueued, as
 * also order of allocation
 */
void
ctsmc_initQ(ctsmc_queue_t *queue, uint16_t size, uint16_t init_size,
		uint8_t startval, int useList)
{
	int i;

	if (size > MAX_Q_SZ || init_size > MAX_Q_SZ)
		return;

	qUse(queue) = useList;
	qSize(queue) = size;
	qCount(queue) = init_size;
	qFront(queue) = qEnd(queue) = 0;

	if (useList != 0) {
		qEntry(queue) = (uint8_t *)kmem_zalloc(size * sizeof (uint8_t),
			KM_SLEEP);

	for (i = 0; i < qSize(queue); i++)
		qEntry(queue)[i] = startval + i;
	}
}

/*
 * Frees memory allocated in a queue
 */
void
ctsmc_freeQ(ctsmc_queue_t *queue)
{
	if (qUse(queue) != 0) {
		kmem_free(qEntry(queue),
			qSize(queue) * (sizeof (uint8_t)));
	}

	qEntry(queue) = NULL;
}

/*
 * Add a groups of entries to queue.
 */
int
ctsmc_enQ(ctsmc_queue_t *queue, uint8_t count, uint8_t *value)
{
	int i;

	if (count == 0 || qCount(queue) + count > qSize(queue))
		return (B_FALSE);

	qCount(queue) += count;
	for (i = 0; i < count; i++) {
		if (qUse(queue) == 0)
			value[i] = qEnd(queue);
		else
			qEntry(queue)[qEnd(queue)] = value[i];

		qEnd(queue)++;
		if (qEnd(queue) >= qSize(queue))
			qEnd(queue) = 0;
	}

	return (B_TRUE);

}

/*
 * Removes a groups of entries from queue.
 */
int
ctsmc_deQ(ctsmc_queue_t *queue, uint8_t count, uint8_t *value)
{
	int i;

	if (count == 0 || qCount(queue) < count)
		return (B_FALSE);

	qCount(queue) -= count;
	for (i = 0; i < count; i++) {
		if (qUse(queue) == 0)
			value[i] = qFront(queue);
		else
			value[i] = qEntry(queue)[qFront(queue)];

		qFront(queue)++;
		if (qFront(queue) >= qSize(queue))
			qFront(queue) = 0;
	}

	return (B_TRUE);
}

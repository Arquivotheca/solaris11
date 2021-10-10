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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Common implementation for priority queues.  Currently we have only a single
 * priority queue per handle, so this may be a little overboard, but this
 * isolates the implementation and allows us to easily expand in the future.
 */

#include <shadow_impl.h>

int
shadow_pq_enqueue(shadow_pq_t *pqp, void *p)
{
	uint_t i, j;
	uint64_t ip, jp;

	if (pqp->shpq_last + 1 == pqp->shpq_size) {
		void **items;
		uint_t size = pqp->shpq_size * 2;

		items = shadow_zalloc(sizeof (void *) * size);
		if (items == NULL)
			return (-1);

		bcopy(pqp->shpq_items, items, sizeof (void *) * pqp->shpq_size);
		free(pqp->shpq_items);
		pqp->shpq_items = items;
		pqp->shpq_size = size;
	}

	assert(pqp->shpq_last + 1 < pqp->shpq_size);

	i = ++pqp->shpq_last;
	pqp->shpq_items[i] = p;
	ip = pqp->shpq_priority(p);

	/*
	 * Bubble the node up until heap criteria are met.
	 */
	for (; i != 1; i = j) {
		j = i / 2;
		jp = pqp->shpq_priority(pqp->shpq_items[j]);

		if (jp < ip)
			break;

		/* Swap parent and child. */
		p = pqp->shpq_items[i];
		pqp->shpq_items[i] = pqp->shpq_items[j];
		pqp->shpq_items[j] = p;
	}

	return (0);
}

static void
shadow_pq_remove_common(shadow_pq_t *pqp, uint_t i)
{
	uint_t c;
	uint64_t ip, cp0, cp1, cp;
	void *p;

	assert(i <= pqp->shpq_last && i != 0);
	assert(pqp->shpq_items[i] != NULL);

	pqp->shpq_items[i] = pqp->shpq_items[pqp->shpq_last];
	pqp->shpq_items[pqp->shpq_last] = NULL;
	pqp->shpq_last--;

	if (pqp->shpq_last < i)
		return;

	ip = pqp->shpq_priority(pqp->shpq_items[i]);

	for (; i * 2 <= pqp->shpq_last; i = c) {
		if (pqp->shpq_items[i * 2 + 1] == NULL) {
			if (pqp->shpq_items[i * 2] == NULL)
				break;
			c = i * 2;
			cp = pqp->shpq_priority(pqp->shpq_items[c]);
		} else {
			assert(pqp->shpq_items[i * 2] != NULL);
			cp0 = pqp->shpq_priority(pqp->shpq_items[i * 2]);
			cp1 = pqp->shpq_priority(pqp->shpq_items[i * 2 + 1]);

			/* Choose the child with lower priority. */
			if (cp0 < cp1) {
				c = i * 2;
				cp = cp0;
			} else {
				c = i * 2 + 1;
				cp = cp1;
			}
		}

		if (ip <= cp)
			break;

		/* Swap parent and (smaller) child. */
		p = pqp->shpq_items[i];
		pqp->shpq_items[i] = pqp->shpq_items[c];
		pqp->shpq_items[c] = p;
	}

	/*
	 * Try to shrink the table if it's more than four times the required
	 * size. If we can't allocate memory, just press on.
	 */
	if (pqp->shpq_last * 4 < pqp->shpq_size) {
		void **items;
		uint_t size = pqp->shpq_size / 2;
		while (size * 2 < pqp->shpq_size) {
			size /= 2;
		}

		items = shadow_alloc(sizeof (void *) * size);
		if (items != NULL) {
			bcopy(pqp->shpq_items, items, sizeof (void *) * size);
			free(pqp->shpq_items);
			pqp->shpq_items = items;
			pqp->shpq_size = size;
		}
	}
}

void *
shadow_pq_dequeue(shadow_pq_t *pqp)
{
	void *p;

	if (pqp->shpq_last == 0)
		return (NULL);

	p = pqp->shpq_items[1];
	shadow_pq_remove_common(pqp, 1);
	return (p);
}

void *
shadow_pq_peek(shadow_pq_t *pqp)
{
	if (pqp->shpq_last == 0)
		return (NULL);

	return (pqp->shpq_items[1]);
}

/*
 * Remove an element from the heap. This is a O(n) operation so should not be
 * used for common operations. This could be improved (to O(log n)) by having
 * each node keep track of its index.
 */
int
shadow_pq_remove(shadow_pq_t *pqp, void *p)
{
	uint_t i;

	for (i = 1; i <= pqp->shpq_last; i++) {
		if (pqp->shpq_items[i] == p) {
			shadow_pq_remove_common(pqp, i);
			return (0);
		}
	}

	return (-1);
}

int
shadow_pq_iter(shadow_pq_t *pqp, int (*callback)(shadow_pq_t *, void *,
    void *), void *data)
{
	uint_t i;
	int ret;

	for (i = 1; i <= pqp->shpq_last; i++) {
		if ((ret = callback(pqp, pqp->shpq_items[i], data)) != 0)
			return (ret);
	}

	return (0);
}

int
shadow_pq_init(shadow_pq_t *pqp, shadow_pq_priority_f *cb)
{
	pqp->shpq_size = (1 << 2);	/* must be a power of two */
	pqp->shpq_last = 0;
	pqp->shpq_priority = cb;
	pqp->shpq_items = shadow_zalloc(sizeof (void *) * pqp->shpq_size);

	return (pqp->shpq_items == NULL ? -1 : 0);
}

void
shadow_pq_fini(shadow_pq_t *pqp)
{
	free(pqp->shpq_items);
	bzero(pqp, sizeof (shadow_pq_t));
}

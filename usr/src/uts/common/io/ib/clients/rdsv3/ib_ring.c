/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains code imported from the OFED rds source file ib_ring.c
 * Oracle elects to have and use the contents of ib_ring.c under and governed
 * by the OpenIB.org BSD license (see below for full license text). However,
 * the following notice accompanied the original version of this file:
 */

/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <sys/rds.h>

#include <sys/ib/clients/rdsv3/rdsv3.h>
#include <sys/ib/clients/rdsv3/ib.h>
#include <sys/ib/clients/rdsv3/rdsv3_debug.h>

/*
 * Locking for IB rings.
 * We assume that allocation is always protected by a mutex
 * in the caller (this is a valid assumption for the current
 * implementation).
 *
 * Freeing always happens in an interrupt, and hence only
 * races with allocations, but not with other free()s.
 *
 * The interaction between allocation and freeing is that
 * the alloc code has to determine the number of free entries.
 * To this end, we maintain two counters; an allocation counter
 * and a free counter. Both are allowed to run freely, and wrap
 * around.
 * The number of used entries is always (alloc_ctr - free_ctr) % NR.
 *
 * The current implementation makes free_ctr atomic. When the
 * caller finds an allocation fails, it should set an "alloc fail"
 * bit and retry the allocation. The "alloc fail" bit essentially tells
 * the CQ completion handlers to wake it up after freeing some
 * more entries.
 */

void
rdsv3_ib_ring_init(struct rdsv3_ib_work_ring *ring, uint32_t nr)
{
	(void) memset(ring, 0, sizeof (*ring));
	ring->w_nr = nr;
	RDSV3_DPRINTF5("rdsv3_ib_ring_init", "ring %p nr %u", ring, ring->w_nr);
}

static inline uint32_t
__rdsv3_ib_ring_used(struct rdsv3_ib_work_ring *ring)
{
	uint32_t diff;

	/* This assumes that atomic_t has at least as many bits as uint32_t */
	diff = ring->w_alloc_ctr - (uint32_t)atomic_get(&ring->w_free_ctr);
	ASSERT(diff <= ring->w_nr);

	return (diff);
}

void
rdsv3_ib_ring_resize(struct rdsv3_ib_work_ring *ring, uint32_t nr)
{
	/*
	 * We only ever get called from the connection setup code,
	 * prior to creating the QP.
	 */
	ASSERT(!__rdsv3_ib_ring_used(ring));
	ring->w_nr = nr;
}

static int
__rdsv3_ib_ring_empty(struct rdsv3_ib_work_ring *ring)
{
	return (__rdsv3_ib_ring_used(ring) == 0);
}

uint32_t
rdsv3_ib_ring_alloc(struct rdsv3_ib_work_ring *ring, uint32_t val,
    uint32_t *pos)
{
	uint32_t ret = 0, avail;

	avail = ring->w_nr - __rdsv3_ib_ring_used(ring);

	RDSV3_DPRINTF5("rdsv3_ib_ring_alloc",
	    "ring %p val %u next %u free %u", ring, val,
	    ring->w_alloc_ptr, avail);

	if (val && avail) {
		ret = min(val, avail);
		*pos = ring->w_alloc_ptr;

		ring->w_alloc_ptr = (ring->w_alloc_ptr + ret) % ring->w_nr;
		ring->w_alloc_ctr += ret;
	}

	return (ret);
}

uint32_t
rdsv3_ib_send_ring_alloc(struct rdsv3_ib_work_ring *ring, uint32_t val,
    uint32_t *pos)
{
	if (val <= (ring->w_nr - __rdsv3_ib_ring_used(ring))) {
		*pos = ring->w_alloc_ptr;

		ring->w_alloc_ptr = (ring->w_alloc_ptr + val) % ring->w_nr;
		ring->w_alloc_ctr += val;
		return (val);
	}

	return (0);
}

void
rdsv3_ib_ring_free(struct rdsv3_ib_work_ring *ring, uint32_t val)
{
	ring->w_free_ptr = (ring->w_free_ptr + val) % ring->w_nr;
	atomic_add_32(&ring->w_free_ctr, val);

	if (__rdsv3_ib_ring_empty(ring))
		rdsv3_wake_up(&ring->w_empty_wait);
}

void
rdsv3_ib_send_ring_free(struct rdsv3_ib_work_ring *ring, uint32_t val)
{
	ring->w_free_ptr = (ring->w_free_ptr + val) % ring->w_nr;
	atomic_add_32(&ring->w_free_ctr, val);
}

void
rdsv3_ib_ring_unalloc(struct rdsv3_ib_work_ring *ring, uint32_t val)
{
	ring->w_alloc_ptr = (ring->w_alloc_ptr - val) % ring->w_nr;
	ring->w_alloc_ctr -= val;
}

int
rdsv3_ib_ring_empty(struct rdsv3_ib_work_ring *ring)
{
	return (__rdsv3_ib_ring_empty(ring));
}

int
rdsv3_ib_ring_low(struct rdsv3_ib_work_ring *ring)
{
	return (__rdsv3_ib_ring_used(ring) <= (ring->w_nr >> 2));
}

/*
 * returns the oldest alloced ring entry.  This will be the next one
 * freed.  This can't be called if there are none allocated.
 */
uint32_t
rdsv3_ib_ring_oldest(struct rdsv3_ib_work_ring *ring)
{
	return (ring->w_free_ptr);
}

/*
 * returns the number of completed work requests.
 */

uint32_t
rdsv3_ib_ring_completed(struct rdsv3_ib_work_ring *ring,
    uint32_t wr_id, uint32_t oldest)
{
	uint32_t ret;

	if (oldest <= (unsigned long long)wr_id)
		ret = (unsigned long long)wr_id - oldest + 1;
	else
		ret = ring->w_nr - oldest + (unsigned long long)wr_id + 1;

	RDSV3_DPRINTF5("rdsv3_ib_ring_completed",
	    "ring %p ret %u wr_id %u oldest %u", ring, ret, wr_id, oldest);
	return (ret);
}

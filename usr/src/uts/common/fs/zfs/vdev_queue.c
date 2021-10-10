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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/avl.h>
#include <sys/io_tag.h>

/*
 * These tunables are for performance analysis.
 */
/*
 * zfs_vdev_max_pending is the maximum number of i/os concurrently
 * pending to each device.  zfs_vdev_min_pending is the initial number
 * of i/os pending to each device (before it starts ramping up to
 * max_pending).  zfs_vdev_future_pending is the maximum number of
 * i/os with deadline in the future concurrently pending to each device.
 */
int zfs_vdev_max_pending = 10;
int zfs_vdev_min_pending = 4;
int zfs_vdev_future_pending = 10;

/* deadline = pri + DEADLINE_NOW */
#define	DEADLINE_NOW (ddi_get_lbolt64() >> zfs_vdev_time_shift)
int zfs_vdev_time_shift = 6;

/* exponential I/O issue ramp-up rate */
int zfs_vdev_ramp_rate = 2;

/*
 * To reduce IOPs, we aggregate small adjacent I/Os into one large I/O.
 * For read I/Os, we also aggregate across small adjacency gaps; for writes
 * we include spans of optional I/Os to aid aggregation at the disk even when
 * they aren't able to help us aggregate at this level.
 */
int zfs_vdev_aggregation_limit = SPA_128KBLOCKSIZE;
int zfs_vdev_read_gap_limit = 32 << 10;
int zfs_vdev_write_gap_limit = 4 << 10;

/*
 * Enable multiple IO vectors when aggregating buffers
 */
#if defined(__sparc)
int zfs_vdev_enable_mvector = 0;
#else
int zfs_vdev_enable_mvector = 1;
#endif
uint_t zfs_mvector_max_iovecs = 64;
uint_t zfs_mvector_max_size = SPA_MAXBLOCKSIZE;

/*
 * scratch buffer for throw away reads/writes
 */
void *zio_scratch_write_buf;
void *zio_scratch_read_buf;

/*
 * Virtual device vector for disk I/O scheduling.
 */
int
vdev_queue_deadline_compare(const void *x1, const void *x2)
{
	const zio_t *z1 = x1;
	const zio_t *z2 = x2;

	if (z1->io_deadline < z2->io_deadline)
		return (-1);
	if (z1->io_deadline > z2->io_deadline)
		return (1);

	if (z1->io_offset < z2->io_offset)
		return (-1);
	if (z1->io_offset > z2->io_offset)
		return (1);

	if (z1 < z2)
		return (-1);
	if (z1 > z2)
		return (1);

	return (0);
}

int
vdev_queue_offset_compare(const void *x1, const void *x2)
{
	const zio_t *z1 = x1;
	const zio_t *z2 = x2;

	if (z1->io_offset < z2->io_offset)
		return (-1);
	if (z1->io_offset > z2->io_offset)
		return (1);

	if (z1 < z2)
		return (-1);
	if (z1 > z2)
		return (1);

	return (0);
}

void
vdev_queue_init(vdev_t *vd)
{
	vdev_queue_t *vq = &vd->vdev_queue;

	mutex_init(&vq->vq_lock, NULL, MUTEX_DEFAULT, NULL);

	avl_create(&vq->vq_deadline_tree, vdev_queue_deadline_compare,
	    sizeof (zio_t), offsetof(struct zio, io_deadline_node));

	avl_create(&vq->vq_read_tree, vdev_queue_offset_compare,
	    sizeof (zio_t), offsetof(struct zio, io_offset_node));

	avl_create(&vq->vq_write_tree, vdev_queue_offset_compare,
	    sizeof (zio_t), offsetof(struct zio, io_offset_node));

	avl_create(&vq->vq_pending_tree, vdev_queue_offset_compare,
	    sizeof (zio_t), offsetof(struct zio, io_offset_node));
}

void
vdev_queue_fini(vdev_t *vd)
{
	vdev_queue_t *vq = &vd->vdev_queue;

	avl_destroy(&vq->vq_deadline_tree);
	avl_destroy(&vq->vq_read_tree);
	avl_destroy(&vq->vq_write_tree);
	avl_destroy(&vq->vq_pending_tree);

	mutex_destroy(&vq->vq_lock);
}

static void
vdev_queue_io_add(vdev_queue_t *vq, zio_t *zio)
{
	avl_add(&vq->vq_deadline_tree, zio);
	avl_add(zio->io_vdev_tree, zio);
}

static void
vdev_queue_io_remove(vdev_queue_t *vq, zio_t *zio)
{
	avl_remove(&vq->vq_deadline_tree, zio);
	avl_remove(zio->io_vdev_tree, zio);
}

static void
vdev_queue_agg_io_done(zio_t *aio)
{
	zio_t *pio;

	/* If we were uing an mvector, free it up */
	if (aio->io_flags & ZIO_FLAG_MVECTOR) {
		io_mvector_free(aio->io_mvector);
		aio->io_flags &= ~ZIO_FLAG_MVECTOR;

	/*
	 * We're not using an mvector. Therefore, we have an aggregation buffer
	 * which needs to be freed. In addition, if this is a read, we need to
	 * copy the data from the aggregation buffer into the individual zios
	 * (before we free the agregation buffer).
	 */
	} else {
		if (aio->io_type == ZIO_TYPE_READ)
			while ((pio = zio_walk_parents(aio)) != NULL)
				bcopy((char *)aio->io_data + (pio->io_offset -
				    aio->io_offset), pio->io_data,
				    pio->io_size);

		zio_buf_free(aio->io_data, aio->io_size);
	}
}

/*
 * Compute the range spanned by two i/os, which is the endpoint of the last
 * (lio->io_offset + lio->io_size) minus start of the first (fio->io_offset).
 * Conveniently, the gap between fio and lio is given by -IO_SPAN(lio, fio);
 * thus fio and lio are adjacent if and only if IO_SPAN(lio, fio) == 0.
 */
#define	IO_SPAN(fio, lio) ((lio)->io_offset + (lio)->io_size - (fio)->io_offset)
#define	IO_GAP(fio, lio) (-IO_SPAN(lio, fio))

static zio_t *
vdev_queue_io_to_issue(vdev_queue_t *vq, uint64_t pending_limit)
{
	zio_t *fio, *lio, *aio, *dio, *nio, *mio;
	avl_tree_t *t;
	int flags;
	uint64_t maxspan;
	uint64_t maxgap;
	int stretch;
	void *ziobuf;
	int iovec_cnt = 1;
	io_mvector_t *mvector;
	io_vector_t *iovec;
	int mvecnocache = 0;
	boolean_t overlapping_io;
	ulong_t pending;
	int i;

again:
	ASSERT(MUTEX_HELD(&vq->vq_lock));

	if ((pending = avl_numnodes(&vq->vq_pending_tree)) >= pending_limit)
		return (NULL);

	fio = lio = avl_first(&vq->vq_deadline_tree);

	/*
	 * For I/Os with deadline in the future do not storm the queue in order
	 * to improve latency for high priority incoming I/Os
	 */
	if (fio == NULL ||
	    fio->io_deadline > DEADLINE_NOW &&
	    pending >= zfs_vdev_future_pending)
		return (NULL);

	t = fio->io_vdev_tree;
	flags = fio->io_flags & ZIO_FLAG_AGG_INHERIT;
	maxgap = (t == &vq->vq_read_tree) ? zfs_vdev_read_gap_limit : 0;

	if (!(flags & ZIO_FLAG_DONT_AGGREGATE)) {
		/*
		 * We can aggregate I/Os that are sufficiently adjacent and of
		 * the same flavor, as expressed by the AGG_INHERIT flags.
		 * The latter requirement is necessary so that certain
		 * attributes of the I/O, such as whether it's a normal I/O
		 * or a scrub/resilver, can be preserved in the aggregate.
		 * We can include optional I/Os, but don't allow them
		 * to begin a range as they add no benefit in that situation.
		 */

		/*
		 * If we are using an mvector, we can increase maxspan. Before
		 * we can do that, we need to add code to ensure we don't build
		 * a DMA that is larger than the HBA can handle (forcing
		 * partial DMAs, which are slow).
		 */
		if (zfs_vdev_enable_mvector) {
			maxspan = zfs_mvector_max_size;
			overlapping_io = B_FALSE;
		} else {
			maxspan = zfs_vdev_aggregation_limit;
		}

		/*
		 * We keep track of the last non-optional I/O.
		 */
		mio = (fio->io_flags & ZIO_FLAG_OPTIONAL) ? NULL : fio;

		/*
		 * Walk backwards through sufficiently contiguous I/Os
		 * recording the last non-option I/O.
		 */
		while ((dio = AVL_PREV(t, fio)) != NULL &&
		    (dio->io_flags & ZIO_FLAG_AGG_INHERIT) == flags &&
		    IO_SPAN(dio, lio) <= maxspan &&
		    IO_GAP(dio, fio) <= maxgap) {
			/*
			 * Track how many individual IOs we have in the
			 * aggregated IO.
			 *
			 * If the gap is negative, there are overlapping IOs.
			 * For this case, we will fall back to using an
			 * aggregate buffer.
			 *
			 * If there is a positive gap, we will need to insert an
			 * extra io_vector to handle it (this can only happen
			 * on reads since write gaps are handled with optional
			 * writes). For read holes, we need to disable the
			 * vdev cache for this aio read since the read buffer
			 * for the hole is shared by multiple aio's (i.e. the
			 * data is thrown away). We only do this is we are
			 * using an mvector.
			 *
			 * NOTE: In the current zfs code, we will truncate a
			 * aggregation before the overlapping IO because maxgap
			 * is an unsigned var. It is not sufficient to change
			 * this to a signed value since the zios are sorted by
			 * io_offset. The last IO in an aggregation can be
			 * fully contained within the previous IO (i.e.
			 * io_offset is >, but (io_offset + len) is less than
			 * the previous IO). For this case, IO_SPAN would
			 * return an incorrect result (this has been observed
			 * during testing with a signed maxgap).
			 */
			offset_t gap = IO_GAP(dio, fio);
			if (gap == 0) {
				iovec_cnt++;
			} else if (gap < 0) {
				overlapping_io = B_TRUE;
			} else {
				ASSERT(dio->io_type == ZIO_TYPE_READ);
				iovec_cnt += 2;
				mvecnocache = ZIO_FLAG_DONT_CACHE;
			}

			fio = dio;
			if (mio == NULL && !(fio->io_flags & ZIO_FLAG_OPTIONAL))
				mio = fio;
		}

		/*
		 * Skip any initial optional I/Os.
		 */
		while ((fio->io_flags & ZIO_FLAG_OPTIONAL) && fio != lio) {
			nio = AVL_NEXT(t, fio);

			/*
			 * We are trimming optional writes off the front of
			 * the aggregation. Adjust iovec_cnt taking into
			 * account any gaps which are present.
			 */
			if (IO_GAP(fio, nio) == 0) {
				iovec_cnt--;
			} else {
				iovec_cnt -= 2;
			}

			fio = nio;
			ASSERT(fio != NULL);
		}

		/*
		 * Walk forward through sufficiently contiguous I/Os.
		 */
		while ((dio = AVL_NEXT(t, lio)) != NULL &&
		    (dio->io_flags & ZIO_FLAG_AGG_INHERIT) == flags &&
		    IO_SPAN(fio, dio) <= maxspan &&
		    IO_GAP(lio, dio) <= maxgap) {
			/*
			 * Track how many individual IOs we have in the
			 * aggregated IO. If there is a positive gap, we will
			 * need to insert in an extra io_vector to handle it
			 * (this can only happen on reads). If the gap is
			 * negative, there are overlapping IOs. For this case,
			 * we will fall back to using an aggregate buffer
			 * later on.
			 */
			offset_t gap = IO_GAP(lio, dio);
			if (gap == 0) {
				iovec_cnt++;
			} else if (gap < 0) {
				overlapping_io = B_TRUE;
			} else {
				iovec_cnt += 2;
				mvecnocache = ZIO_FLAG_DONT_CACHE;
			}

			lio = dio;
			if (!(lio->io_flags & ZIO_FLAG_OPTIONAL))
				mio = lio;
		}

		/*
		 * Now that we've established the range of the I/O aggregation
		 * we must decide what to do with trailing optional I/Os.
		 * For reads, there's nothing to do. While we are unable to
		 * aggregate further, it's possible that a trailing optional
		 * I/O would allow the underlying device to aggregate with
		 * subsequent I/Os. We must therefore determine if the next
		 * non-optional I/O is close enough to make aggregation
		 * worthwhile.
		 */
		stretch = B_FALSE;
		if (t != &vq->vq_read_tree && mio != NULL) {
			nio = lio;
			while ((dio = AVL_NEXT(t, nio)) != NULL &&
			    IO_GAP(nio, dio) == 0 &&
			    IO_GAP(mio, dio) <= zfs_vdev_write_gap_limit) {
				nio = dio;
				if (!(nio->io_flags & ZIO_FLAG_OPTIONAL)) {
					stretch = B_TRUE;
					break;
				}
			}
		}

		if (stretch) {
			/* This may be a no-op. */
			VERIFY((dio = AVL_NEXT(t, lio)) != NULL);
			dio->io_flags &= ~ZIO_FLAG_OPTIONAL;
		} else {
			/*
			 * Trim any optional writes from the end of the
			 * aggregate IO. Since this is a write, there can't
			 * be any gaps. Therefore, we don't need to check for
			 * gaps when adjusting iovec_cnt.
			 */
			while (lio != mio && lio != fio) {
				ASSERT(lio->io_flags & ZIO_FLAG_OPTIONAL);
				lio = AVL_PREV(t, lio);
				ASSERT(lio != NULL);
				iovec_cnt--;
			}
		}
	}

	if (fio != lio) {
		int priority = ZIO_PRIORITY_AGG;
		uint64_t size = IO_SPAN(fio, lio);
		ASSERT(size <= maxspan);

		/*
		 * If we can use an IO mvector, allocate one now. We can pass
		 * the IO mvector all the way down the IO stack. Cap the number
		 * of io_vectors to something reasonable.
		 */
		mvector = NULL;
		if (zfs_vdev_enable_mvector &&
		    (iovec_cnt <= zfs_mvector_max_iovecs) &&
		    !overlapping_io) {
			mvector = io_mvector_alloc(iovec_cnt);
		}
		if (mvector != NULL) {
			mvector->im_len = size;
			ziobuf = NULL;

		/*
		 * We can't use an IO mvector, so allocate an aggregate buffer
		 * to copy the individual IO buffers from/to.
		 */
		} else {
			ziobuf = zio_buf_alloc(size);
		}

		aio = zio_vdev_delegated_io(fio->io_vd, fio->io_offset,
		    ziobuf, size, fio->io_type, priority,
		    flags | ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_QUEUE,
		    vdev_queue_agg_io_done, NULL);

		/*
		 * If we are using an io mvector, add it to the aggregate
		 * zio.
		 */
		if (mvector != NULL) {
			io_tag(&mvector->im_tags, IO_TAG_STATE, "zio_t",
			    (uint64_t)(uintptr_t)aio);
			aio->io_mvector = mvector;
			aio->io_flags |= ZIO_FLAG_MVECTOR;
			aio->io_flags |= mvecnocache;
		}

		i = 0;
		nio = fio;
		do {
			dio = nio;
			nio = AVL_NEXT(t, dio);
			ASSERT(dio->io_type == aio->io_type);
			ASSERT(dio->io_vdev_tree == t);

			/*
			 * In an aggregated IO, if we are using the individual
			 * IO buffers instead of allocating and copying to/from
			 * an aggregation buffer, copy the buffer information
			 * from each IO into a IO mvector that we can pass
			 * down the IO stack.
			 */
			if (aio->io_flags & ZIO_FLAG_MVECTOR) {
				iovec = &aio->io_mvector->im_vects[i];
				iovec->iv_flags = IO_VECTOR_FLAG_KADDR;
				iovec->iv_len = dio->io_size;

				/*
				 * If there isn't any data to write, i.e. this
				 * is an optional IO, use a static buffer which
				 * has been initialized to all zeros.
				 */
				if (dio->io_flags & ZIO_FLAG_NODATA) {
					ASSERT(dio->io_type == ZIO_TYPE_WRITE);
					iovec->iv_kaddr = zio_scratch_write_buf;

				/*
				 * This is not an optional write. Use the data
				 * in the zio.
				 */
				} else {
					iovec->iv_kaddr = dio->io_data;
				}

				/*
				 * If this is not the last IO in the
				 * aggregation, check to see if there is a gap
				 * between this IO and the next IO. If there
				 * is, we need to insert a throw away io_vector
				 * that we can read into (this gap can only
				 * happen for reads).
				 */
				if (dio != lio) {
					offset_t gap = IO_GAP(dio, nio);
					if (gap != 0) {
						ASSERT(t == &vq->vq_read_tree);
						i++;
						iovec = &aio->io_mvector->
						    im_vects[i];
						iovec->iv_kaddr =
						    zio_scratch_read_buf;
						iovec->iv_flags =
						    IO_VECTOR_FLAG_KADDR;
						iovec->iv_len = gap;
					}
				}
			} else if (dio->io_flags & ZIO_FLAG_NODATA) {
				ASSERT(dio->io_type == ZIO_TYPE_WRITE);
				bzero((char *)aio->io_data + (dio->io_offset -
				    aio->io_offset), dio->io_size);
			} else if (dio->io_type == ZIO_TYPE_WRITE) {
				bcopy(dio->io_data, (char *)aio->io_data +
				    (dio->io_offset - aio->io_offset),
				    dio->io_size);
			}

			priority = MIN(priority, dio->io_priority);
			zio_add_child(dio, aio);
			vdev_queue_io_remove(vq, dio);
			zio_vdev_io_bypass(dio);
			zio_execute(dio);
			i++;
		} while (dio != lio);

		aio->io_priority = priority;
		avl_add(&vq->vq_pending_tree, aio);

		return (aio);
	}

	ASSERT(fio->io_vdev_tree == t);
	vdev_queue_io_remove(vq, fio);

	/*
	 * If the I/O is or was optional and therefore has no data, we need to
	 * simply discard it. We need to drop the vdev queue's lock to avoid a
	 * deadlock that we could encounter since this I/O will complete
	 * immediately.
	 */
	if (fio->io_flags & ZIO_FLAG_NODATA) {
		mutex_exit(&vq->vq_lock);
		zio_vdev_io_bypass(fio);
		zio_execute(fio);
		mutex_enter(&vq->vq_lock);
		goto again;
	}

	avl_add(&vq->vq_pending_tree, fio);

	return (fio);
}

zio_t *
vdev_queue_io(zio_t *zio)
{
	vdev_queue_t *vq = &zio->io_vd->vdev_queue;
	zio_t *nio;

	ASSERT(zio->io_type == ZIO_TYPE_READ || zio->io_type == ZIO_TYPE_WRITE);

	if (zio->io_flags & ZIO_FLAG_DONT_QUEUE)
		return (zio);

	zio->io_flags |= ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_QUEUE;

	if (zio->io_type == ZIO_TYPE_READ)
		zio->io_vdev_tree = &vq->vq_read_tree;
	else
		zio->io_vdev_tree = &vq->vq_write_tree;

	mutex_enter(&vq->vq_lock);

	zio->io_deadline = DEADLINE_NOW + zio->io_priority;

	vdev_queue_io_add(vq, zio);

	nio = vdev_queue_io_to_issue(vq, zfs_vdev_min_pending);

	mutex_exit(&vq->vq_lock);

	if (nio == NULL)
		return (NULL);

	if (nio->io_done == vdev_queue_agg_io_done) {
		zio_nowait(nio);
		return (NULL);
	}

	return (nio);
}

void
vdev_queue_io_done(zio_t *zio)
{
	vdev_queue_t *vq = &zio->io_vd->vdev_queue;

	mutex_enter(&vq->vq_lock);

	avl_remove(&vq->vq_pending_tree, zio);

	for (int i = 0; i < zfs_vdev_ramp_rate; i++) {
		zio_t *nio = vdev_queue_io_to_issue(vq, zfs_vdev_max_pending);
		if (nio == NULL)
			break;
		mutex_exit(&vq->vq_lock);
		if (nio->io_done == vdev_queue_agg_io_done) {
			zio_nowait(nio);
		} else {
			zio_vdev_io_reissue(nio);
			zio_execute(nio);
		}
		mutex_enter(&vq->vq_lock);
	}

	mutex_exit(&vq->vq_lock);
}

void
vdev_queue_mvec_init(void)
{
	/*
	 * Initialize the scratch buffers. zio_scratch_write_buf is a zeroed
	 * out buffer used for an IO write (buffer is read out of).
	 * zio_scratch_read_buf is used for throwaway IO reads (buffer is
	 * written into).
	 */
	zio_scratch_write_buf = kmem_zalloc(SPA_MAXBLOCKSIZE, KM_SLEEP);
	zio_scratch_read_buf = kmem_alloc(SPA_MAXBLOCKSIZE, KM_SLEEP);
}

void
vdev_queue_mvec_fini(void)
{
	kmem_free(zio_scratch_read_buf, SPA_MAXBLOCKSIZE);
	kmem_free(zio_scratch_write_buf, SPA_MAXBLOCKSIZE);
}

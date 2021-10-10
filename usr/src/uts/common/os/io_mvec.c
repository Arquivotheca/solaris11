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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/list.h>
#include <sys/sysmacros.h>
#include <sys/io_mvec.h>
#include <sys/io_mvec_impl.h>

#ifdef	_KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/ksynch.h>
#include <sys/systm.h>
#include <sys/ddidmareq.h>
#include <sys/mman.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/seg_kmem.h>
#include <vm/seg_kpm.h>
#else
#include <sys/zfs_context.h>
#endif


/*
 * When set to non-zero, mvectors will be placed on a list when allocated and
 * removed from the list when freed. Useful for tracking down mvectors in
 * flight on hangs, etc.
 */
#if defined(DEBUG) || !defined(_KERNEL)
int io_mvector_tracking_enable = 1;
#else
int io_mvector_tracking_enable = 0;
#endif


/* list of IOs (limited to mvectors currently) */
struct io_list {
	kmutex_t	il_mutex;
	list_t		il_list;
	uint_t		il_cnt;
};
typedef struct io_list *io_list_t;

/*
 * IO_VECTOR_PRIV_FLAG_MAPIN
 *    set if this individual io_vector has been mapped into KVA.
 * IO_VECTOR_PRIV_FLAG_LOCK
 *    set if this individual io_vector has been locked down.
 */
typedef enum {
	IO_VECTOR_PRIV_FLAG_MAPIN	= (1 << 24),
	IO_VECTOR_PRIV_FLAG_LOCK	= (1 << 25)
} io_vector_priv_flags_t;

/*
 * IO_MVECTOR_PRIV_FLAG_ON_IOLIST
 *    we track if we're on the IO list so we can enable/disable the list on
 *    the fly.
 * IO_MVECTOR_PRIV_FLAG_IOVEC_ALLOC
 *    set if this mvector had to allocate space for the io_vectors instead of
 *    using the pre-allcated space.
 * IO_MVECTOR_PRIV_FLAG_INSYNC
 *    used for a mvector in a buf_t when it's been mapped in. Since we can't
 *    return a single VA for a mvector, and we can't change every driver which
 *    calls bp_mapin(), the buf code will use a bounce buffer when mapping in
 *    a mvector so that it presents a single buffer. This helps that code
 *    track if the bounce buffer has been sync'd already (since there are
 *    multiple paths where this might happen for a read).
 * IO_MVECTOR_PRIV_FLAG_MAPIN
 *    set if the mvector has been mapped into KVA.
 * IO_MVECTOR_PRIV_FLAG_MAPIN_FAST
 *    set if the io_mvector_mapin was called, but we didn't have anything we
 *    had to mapin.
 * IO_MVECTOR_PRIV_FLAG_LOCK
 *    set if the mvector has been locked down.
 * IO_MVECTOR_PRIV_FLAG_LOCK_FAST
 *    set if the io_mvector_lock was called, but we didn't have anything we
 *    had to lock down.
 * IO_MVECTOR_PRIV_FLAG_CLONE
 *    set if this mvector is a clone of another mvector.
 */
typedef enum {
	IO_MVECTOR_PRIV_FLAG_ON_IOLIST		= (1 << 0),
	IO_MVECTOR_PRIV_FLAG_IOVEC_ALLOC	= (1 << 1),
	IO_MVECTOR_PRIV_FLAG_INSYNC		= (1 << 2),
	IO_MVECTOR_PRIV_FLAG_MAPIN		= (1 << 3),
	IO_MVECTOR_PRIV_FLAG_MAPIN_FAST		= (1 << 4),
	IO_MVECTOR_PRIV_FLAG_LOCK		= (1 << 5),
	IO_MVECTOR_PRIV_FLAG_LOCK_FAST		= (1 << 6),
	IO_MVECTOR_PRIV_FLAG_CLONE		= (1 << 7)
} io_mvector_priv_flags_t;

/* mvector public and private state */
#define	IO_MVECTOR_PREALLOC_IOVECS	8
struct io_mvecpriv {
	struct io_mvector	mp_mvector; /* public mvector state */
	io_mvector_priv_flags_t	mp_flags;
	list_node_t		mp_iolist_node;

	/*
	 * preallocate some number of io vectors. If we need more than that,
	 * we'll allocate the space for them dynamically, and free the space
	 * on io_mvector_free (i.e. if IO_MVECTOR_PRIV_FLAG_IOVEC_ALLOC is set)
	 */
	io_vector_t		mp_iovec_prealloc[IO_MVECTOR_PREALLOC_IOVECS];
};

typedef struct io_mvec_state {
	kmem_cache_t	*io_mvector_cache;
	struct io_list	io_list;
} io_mvec_state_t;
io_mvec_state_t io_mvec_state;

typedef void (*i_io_mvcopy_t)(void *mvkaddr, void *buf, size_t size);


extern void physio_bufs_init(void);


/*
 * io_list_init()
 *    initialize IO list (optionally used to track mvectors currently).
 */
static void
io_list_init(io_list_t list)
{
	mutex_init(&list->il_mutex, NULL, MUTEX_DRIVER, NULL);
	list_create(&list->il_list, sizeof (struct io_mvecpriv),
	    offsetof(struct io_mvecpriv, mp_iolist_node));
	list->il_cnt = 0;
}


/*
 * io_list_fini()
 *    cleanup IO list
 */
static void
io_list_fini(io_list_t list)
{
	ASSERT(list->il_cnt == 0);
	list_destroy(&list->il_list);
	mutex_destroy(&list->il_mutex);
}


/*
 * io_list_add()
 *    add a mvector to the io list.
 */
static void
io_list_add(io_list_t list, io_mvecpriv_t mvecpriv)
{
	mutex_enter(&list->il_mutex);
	list_insert_tail(&list->il_list, mvecpriv);
	list->il_cnt++;
	mutex_exit(&list->il_mutex);
}


/*
 * io_list_add()
 *    remove a mvector from the io list.
 */
static void
io_list_remove(io_list_t list, io_mvecpriv_t mvecpriv)
{
	mutex_enter(&list->il_mutex);
	list_remove(&list->il_list, mvecpriv);
	list->il_cnt--;
	mutex_exit(&list->il_mutex);
}


/*
 * i_io_mvector_set_insync()
 */
void
i_io_mvector_set_insync(io_mvector_t *mvector)
{
	mvector->im_private->mp_flags |= IO_MVECTOR_PRIV_FLAG_INSYNC;
}


/*
 * i_io_mvector_insync()
 */
boolean_t
i_io_mvector_insync(io_mvector_t *mvector)
{
	if (mvector->im_private->mp_flags & IO_MVECTOR_PRIV_FLAG_INSYNC)
		return (B_TRUE);

	return (B_FALSE);
}


/*
 * i_io_mvector_is_clone()
 */
boolean_t
i_io_mvector_is_clone(io_mvector_t *mvector)
{
	if (mvector->im_private->mp_flags & IO_MVECTOR_PRIV_FLAG_CLONE)
		return (B_TRUE);

	return (B_FALSE);
}


/*
 * i_io_mvector_clone()
 *    create a new mvector, cloning the mvector passed in, starting at "offset"
 *    into the mvector and ending at offset + len. Like bioclone, the cloned
 *    mvector must be freed before the original mvector.
 */
/*ARGSUSED*/
io_mvector_t *
i_io_mvector_clone(io_mvector_t *mvector, offset_t offset, size_t len)
{
	io_mvector_t *cloned_mvector;
	offset_t clone_offset;
	boolean_t searching;
	io_vector_t *iovec;
	size_t clone_len;
	int first_iovec;
	int last_iovec;
	int num_iovecs;
	int i;


	ASSERT((offset + len) <= mvector->im_len);

	/*
	 * figure out which io_vector "offset" starts at and which io_vector
	 * we stop at.
	 */
	clone_offset = offset;
	clone_len = len;
	searching = B_TRUE;
	last_iovec = 0;
	do {
		offset_t initial_offset;

		iovec = &mvector->im_vects[last_iovec];
		if (searching && (clone_offset >= iovec->iv_len)) {
			clone_offset -= iovec->iv_len;
			last_iovec++;
			continue;
		}

		initial_offset = 0;
		if (searching) {
			searching = B_FALSE;
			first_iovec = last_iovec;
			initial_offset = clone_offset;
		}

		if ((clone_len + initial_offset) > iovec->iv_len) {
			clone_len -= (iovec->iv_len - initial_offset);
			last_iovec++;
		}
	} while (searching || (clone_len >
	    mvector->im_vects[last_iovec].iv_len));

	/*
	 * allocate a new mvector for the clone. NOTE: we only replicate the
	 * io_vector state, and not the mvector state. there for things like
	 * im_tags will not be carried over. Also, things like LOCK and MAPIN
	 * will behave correctly in io_mvector_free() since those global flags
	 * will not be set in a clone.
	 */
	num_iovecs = last_iovec - first_iovec + 1;
	ASSERT(num_iovecs <= mvector->im_iovec_cnt);
	cloned_mvector = io_mvector_alloc(num_iovecs);
	cloned_mvector->im_len = len;
	cloned_mvector->im_private->mp_flags |= IO_MVECTOR_PRIV_FLAG_CLONE;

	/* loop through the io_vectors that we are copying into the clone */
	for (i = first_iovec; i < (last_iovec + 1); i++) {

		/*
		 * copy the io_vector into the clone. We'll update any fields
		 * which need to be modified next.
		 */
		iovec = &cloned_mvector->im_vects[i - first_iovec];
		*iovec = mvector->im_vects[i];

		/* don't clone the shadow info */
		iovec->iv_flags &= ~IO_VECTOR_FLAG_SHADOW;
		iovec->iv_shadow = NULL;

		/*
		 * if this is the first io_vector that we are cloning, and we
		 * have an offset into the vector, adjust the size, starting
		 * address/pp, and/or offset to compensate for the "offset"
		 * passed in.
		 */
		if ((i == first_iovec) && (clone_offset != 0)) {
			ASSERT(clone_offset < iovec->iv_len);
			iovec->iv_len -= clone_offset;
			if (iovec->iv_flags & IO_VECTOR_FLAG_KADDR)
				iovec->iv_kaddr = (void *)(
				    (uintptr_t)iovec->iv_kaddr +
				    (uintptr_t)clone_offset);
			if (iovec->iv_flags & IO_VECTOR_FLAG_UVA)
				iovec->iv.ib_uva.iu_addr = (void *)(
				    (uintptr_t)iovec->iv.ib_uva.iu_addr
				    + (uintptr_t)clone_offset);
#ifdef	_KERNEL
			if (iovec->iv_flags & IO_VECTOR_FLAG_PLIST) {
				size_t skipped_pages;
				page_t *pp;

				skipped_pages = btop(
				    iovec->iv.ib_plist.ip_offset +
				    clone_offset);
				iovec->iv.ib_plist.ip_offset =
				    (iovec->iv.ib_plist.ip_offset +
				    clone_offset) & PAGEOFFSET;
				pp = iovec->iv.ib_plist.ip_pp;
				while (skipped_pages > 0) {
					pp = pp->p_next;
					skipped_pages--;
				}
			}
#endif
		}

		/*
		 * if this is the last io_vector we are copying, and we aren't
		 * using the entire mvector, update the length.
		 */
		if ((i == last_iovec) && (clone_len < iovec->iv_len))
			iovec->iv_len = clone_len;
	}

	return (cloned_mvector);
}


#ifdef	_KERNEL
/*
 * i_io_vector_info()
 *    for a given io_vector and offset into the io_vector, return the number
 *    of pages it occupies (adjusting for offset), the page index into the
 *    io_vector where offset starts, and the offset into the page we're
 *    starting at.
 */
static void
i_io_vector_info(io_vector_t *iovec, offset_t offset, size_t *npages,
    size_t *fpgidx, offset_t *pgoff)
{
	offset_t ioffset;

	/* get the initial page offset of the io_vector */
	if (iovec->iv_flags & IO_VECTOR_FLAG_UVA) {
		ioffset = (offset_t)((uintptr_t)iovec->iv.ib_uva.iu_addr &
		    PAGEOFFSET);
	} else if (iovec->iv_flags & IO_VECTOR_FLAG_PLIST) {
		ioffset = iovec->iv.ib_plist.ip_offset;
	} else {
		ASSERT(iovec->iv_flags & IO_VECTOR_FLAG_KADDR);
		ioffset = (offset_t)((uintptr_t)iovec->iv_kaddr & PAGEOFFSET);
	}

	if (npages != NULL)
		*npages = ((ioffset + (iovec->iv_len - offset)) +
		    PAGEOFFSET) >> PAGESHIFT;

	if (fpgidx != NULL)
		*fpgidx = (ioffset + offset) >> PAGESHIFT;

	if (pgoff != NULL)
		*pgoff = (ioffset + offset) & PAGEOFFSET;
}


/*
 * i_io_mvector_mapout()
 *    if the mvector was mapped into KVA, unmap it and free up the KVA space.
 */
static void
i_io_mvector_mapout(io_mvector_t *mvector)
{
	size_t i;

	/*
	 * check to see if we actually mapped in the mvector.  We might have
	 * hit the fast path instead.
	 */
	if (mvector->im_private->mp_flags & IO_MVECTOR_PRIV_FLAG_MAPIN) {
		for (i = 0; i < mvector->im_iovec_cnt; i++) {
			io_vector_t *iovec;

			iovec = &mvector->im_vects[i];
			/*
			 * see if this io_vector was mapped in.. not every
			 * io_vector will need to be mapped in since they can
			 * be different types.
			 */
			if (iovec->iv_flags & IO_VECTOR_PRIV_FLAG_MAPIN) {
				uintptr_t kaddr;
				size_t npages;
				int j;

				/*
				 * unmap all the pages, then free the KVA
				 * space.
				 */
				kaddr = (uintptr_t)iovec->iv_kaddr & PAGEMASK;
				i_io_vector_info(iovec, 0, &npages, NULL, NULL);
				for (j = 0; j < npages; j++) {
					hat_unload(kas.a_hat, (void *)kaddr,
					    PAGESIZE, HAT_UNLOAD_UNLOCK);
					kaddr += PAGESIZE;
				}
				kaddr = (uintptr_t)iovec->iv_kaddr & PAGEMASK;
				vmem_free(heap_arena, (void *)kaddr,
				    ptob(npages));
				iovec->iv_flags &= ~IO_VECTOR_PRIV_FLAG_MAPIN;
			}
		}
		mvector->im_private->mp_flags &= ~IO_MVECTOR_PRIV_FLAG_MAPIN;
	}
}


/*
 * i_io_mvector_unlock()
 *    if portions of the the mvector were locked down, unlock them.
 */
static void
i_io_mvector_unlock(io_mvector_t *mvector)
{
	size_t i;

	/*
	 * check to see if we actually locked in the mvector.  We might have
	 * hit the fast path instead.
	 */
	if (mvector->im_private->mp_flags & IO_MVECTOR_PRIV_FLAG_LOCK) {
		for (i = 0; i < mvector->im_iovec_cnt; i++) {
			io_vector_t *iovec;
			enum seg_rw rwflag;

			/* covert the WRITE flag for as_pageunlock */
			iovec = &mvector->im_vects[i];
			if (iovec->iv_flags & IO_VECTOR_FLAG_WRITE)
				rwflag = S_READ;
			else
				rwflag = S_WRITE;

			/*
			 * see if this io_vector was locked in.. not every
			 * io_vector will need to be locked in since they can
			 * be different types.
			 */
			if (iovec->iv_flags & IO_VECTOR_PRIV_FLAG_LOCK) {
				page_t **pplist = NULL;

				ASSERT(iovec->iv_flags & IO_VECTOR_FLAG_UVA);

				/*
				 * if we have a shadow list, let as_pageunlock
				 * free that up.
				 */
				if (iovec->iv_flags & IO_VECTOR_FLAG_SHADOW)
					pplist = (struct page **)
					    iovec->iv_shadow->is_pplist;

				as_pageunlock(iovec->iv.ib_uva.iu_as, pplist,
				    (caddr_t)iovec->iv.ib_uva.iu_addr,
				    iovec->iv_len, rwflag);

				/*
				 * if we had a shadow list, clean up and state
				 * associated with it.
				 */
				if (iovec->iv_flags & IO_VECTOR_FLAG_SHADOW) {
					kmem_free(iovec->iv_shadow,
					    sizeof (struct io_shadow));
					iovec->iv_flags &=
					    ~IO_VECTOR_FLAG_SHADOW;
				}
				iovec->iv_flags &= ~IO_VECTOR_PRIV_FLAG_LOCK;
			}
		}
		mvector->im_private->mp_flags &= ~IO_MVECTOR_PRIV_FLAG_LOCK;
	}
}
#endif


/*
 * i_io_mvector_copy_common()
 *    copy between a mvector and a kernel buf.  Use the routine pass in to
 *    handle the direction to copy in. offset is an offset into the mvector.
 */
static int
i_io_mvector_copy_common(io_mvector_t *mvector, void *buf, offset_t offset,
    size_t size, i_io_mvcopy_t mvcopy)
{
	io_vector_t *iovec;
	size_t copy_size;
	int i;


	ASSERT((offset + size) <= mvector->im_len);

	/* initial io vector */
	i = 0;

	/* while we still have data to copy */
	while (size > 0) {
		iovec = &mvector->im_vects[i];

		/*
		 * if the current io_vector is before where we want to start
		 * copying from, jump to the next io_vector.
		 */
		if (offset >= iovec->iv_len) {
			offset -= iovec->iv_len;
			i++;
			continue;
		}

#ifdef	_KERNEL
		/*
		 * if we don't have a kernel address, and kpm is not available,
		 * map in the mvector. io_mvector_mapin() will keep track if
		 * the mvector has already been mapped in. mapin will also
		 * lock down the mvector.
		 */
		if ((iovec->iv_flags & IO_VECTOR_FLAG_KADDR) == 0) {
			if (!kpm_enable) {
				int rc;
				ASSERT(!(iovec->iv_flags &
				    IO_VECTOR_PRIV_FLAG_MAPIN));
				rc = io_mvector_mapin(mvector, VM_SLEEP);
				if (rc != 0)
					return (rc);
			}
		}
#endif

		/*
		 * for the number of bytes to copy, handle an offset into the
		 * io_vector and the case where we are copying less than the
		 * number of bytes left in the io_vector.
		 */
		if (offset > 0)
			copy_size = MIN(iovec->iv_len - offset, size);
		else
			copy_size = MIN(iovec->iv_len, size);

		/*
		 * if we have a kernel address, we can copy from this
		 * io_vector.
		 */
		if (iovec->iv_flags & IO_VECTOR_FLAG_KADDR) {
			uintptr_t kaddr;

			/* if we have an intial offset, adjust for that */
			if (offset > 0) {
				kaddr = (uintptr_t)iovec->iv_kaddr + offset;
				offset = 0;
			} else {
				kaddr = (uintptr_t)iovec->iv_kaddr;
			}
			(*mvcopy)((void *)kaddr, (void *)buf, copy_size);

		/*
		 * we don't have a kaddr, use kpm for the mvector to do the
		 * copy. if kpm wasn't available, we would have mapped in the
		 * mvector and hit the code path above.
		 */
		}
#ifdef	_KERNEL
		else {
			uintptr_t uaddr;
			uintptr_t lbuf;
			offset_t pgoff;
			size_t fpgidx;
			size_t csize;
			page_t *pp;
			size_t j;


			ASSERT(kpm_enable);

			/* if the mvector hasn't been locked down, do that */
			if (!(iovec->iv_flags & IO_VECTOR_PRIV_FLAG_LOCK)) {
				int rc;
				rc = io_mvector_lock(mvector);
				if (rc != 0)
					return (rc);
			}

			/*
			 * from an offset into the io_vector, get the index
			 * of the first page we are starting at, and the offset
			 * into that page.
			 */
			i_io_vector_info(iovec, offset, NULL, &fpgidx, &pgoff);

			if (iovec->iv_flags & IO_VECTOR_FLAG_UVA)
				uaddr = (uintptr_t)iovec->iv.ib_uva.iu_addr &
				    PAGEMASK;
			else
				pp = iovec->iv.ib_plist.ip_pp;

			/* skip over pages before offset */
			for (j = 0; j < fpgidx; j++) {
				if (iovec->iv_flags & IO_VECTOR_FLAG_UVA)
					uaddr += PAGESIZE;
				else
					pp = pp->p_next;
			}

			lbuf = (uintptr_t)buf;
			csize = copy_size;

			/*
			 * while we have bytes to copy, copy the bytes one
			 * page at a time.
			 */
			while (csize > 0) {
				uintptr_t kaddr;
				size_t pgsize;
				pfn_t pfn;

				/* get the PFN, then update for next page */
				if (iovec->iv_flags & IO_VECTOR_FLAG_UVA) {
					pfn = hat_getpfnum(
					    iovec->iv.ib_uva.iu_as->a_hat,
					    (caddr_t)uaddr);
					uaddr += PAGESIZE;
				} else {
					pfn = pp->p_pagenum;
					pp = pp->p_next;
				}

				/* copy the bytes */
				kaddr = (uintptr_t)(hat_kpm_mapin_pfn(pfn) +
				    pgoff);
				pgsize = MIN((PAGESIZE - pgoff), csize);
				(*mvcopy)((void *)kaddr, (void *)lbuf, pgsize);
				hat_kpm_mapout_pfn(pfn);

				lbuf += pgsize;
				csize -= pgsize;
				pgoff = 0;
			}
		}
#endif

		/*
		 * adjust for what we did in the current io_vector, then go
		 * to the next one.
		 */
		size -= copy_size;
		buf = (void *)((uintptr_t)buf + copy_size);
		i++;
	}

	return (0);
}


/*
 * i_io_copyout()
 *    copy from mvkaddr to buf
 */
static void
i_io_copyout(void *mvkaddr, void *buf, size_t size)
{
	bcopy(mvkaddr, buf, size);
}


/*
 * i_io_copyin()
 *    copy from buf into mvkaddr
 */
static void
i_io_copyin(void *mvkaddr, void *buf, size_t size)
{
	bcopy(buf, mvkaddr, size);
}


/*
 * io_mvector_alloc()
 *    allocate public and private state for the mvector for the passed in
 *    (iovec_cnt) number of io_vectors.
 */
/*ARGSUSED*/
io_mvector_t *
io_mvector_alloc(uint_t iovec_cnt)
{
#if defined(__sparc)
	return (NULL);
#else
	io_mvecpriv_t mvecpriv;
	io_mvector_t *mvector;


	mvecpriv = kmem_cache_alloc(io_mvec_state.io_mvector_cache, KM_SLEEP);
	mvecpriv->mp_flags = 0;
	mvector = &mvecpriv->mp_mvector;
	mvector->im_private = mvecpriv;
	mvector->im_iovec_cnt = iovec_cnt;
	mvector->im_tags = NULL;

	/*
	 * if the number of io_vectors we need is less than or equal to the
	 * number we have preallocated, use the preallocated buffer. Else,
	 * we'll need to allocate space for the io_vectors too.
	 */
	if (mvector->im_iovec_cnt <= IO_MVECTOR_PREALLOC_IOVECS) {
		mvector->im_vects = mvecpriv->mp_iovec_prealloc;
	} else {
		mvecpriv->mp_flags |= IO_MVECTOR_PRIV_FLAG_IOVEC_ALLOC;
		mvector->im_vects = kmem_alloc(sizeof (struct io_vector) *
		    mvector->im_iovec_cnt, KM_SLEEP);
	}

	/* if mvector tracking is enabled, put this mvector on the list */
	if (io_mvector_tracking_enable) {
		mvecpriv->mp_flags |= IO_MVECTOR_PRIV_FLAG_ON_IOLIST;
		io_list_add(&io_mvec_state.io_list, mvecpriv);
	}

	return (mvector);
#endif
}


/*
 * io_mvector_free()
 *    free up mvector state
 */
void
io_mvector_free(io_mvector_t *mvector)
{
	io_mvecpriv_t mvecpriv;


	mvecpriv = mvector->im_private;

	/* if we have attached IO tags to the mvector, free them up */
	if (mvector->im_tags != NULL)
		io_tag_destroy(&mvector->im_tags);

#ifdef	_KERNEL
	/* if we have mapped in the mvector, unmap it */
	if (mvector->im_private->mp_flags & IO_MVECTOR_PRIV_FLAG_MAPIN)
		i_io_mvector_mapout(mvector);

	/* if we have locked in the mvector, unlock it */
	if (mvector->im_private->mp_flags & IO_MVECTOR_PRIV_FLAG_LOCK)
		i_io_mvector_unlock(mvector);
#endif

	/* if the mvector is on the io list, remove it */
	if (mvecpriv->mp_flags & IO_MVECTOR_PRIV_FLAG_ON_IOLIST)
		io_list_remove(&io_mvec_state.io_list, mvecpriv);

	/* if we had to allocate space for the io_vectors, free it */
	if (mvecpriv->mp_flags & IO_MVECTOR_PRIV_FLAG_IOVEC_ALLOC)
		kmem_free(mvector->im_vects, sizeof (struct io_vector) *
		    mvector->im_iovec_cnt);

	/* free up the mvector */
	kmem_cache_free(io_mvec_state.io_mvector_cache, mvecpriv);
}


#ifdef	_KERNEL
/*
 * io_mvector_lock()
 *    ensure all io_vectors are locked down (for UVAs), and populate a shadow
 *    list for all io_vectors (UVA and KVA). Any locked down memory will be
 *    unlocked during io_mvector_free().
 */
/*ARGSUSED*/
int
io_mvector_lock(io_mvector_t *mvector)
{
	int i;


	/* if we've already locked this mvector, nothing to do */
	if (mvector->im_private->mp_flags &
	    (IO_MVECTOR_PRIV_FLAG_LOCK | IO_MVECTOR_PRIV_FLAG_LOCK_FAST))
		return (0);

	/*
	 * KVA and PLISTs are locked down already. If we have a UVA, we need
	 * to check to see if it needs to be locked down.
	 */
	for (i = 0; i < mvector->im_iovec_cnt; i++)
		if (mvector->im_vects[i].iv_flags & IO_VECTOR_FLAG_UVA)
			break;

	/*
	 * if all of the io_vectors have a KVA, we hit the fast path and we
	 * are done.
	 */
	if (i >= mvector->im_iovec_cnt) {
		mvector->im_private->mp_flags |=
		    IO_MVECTOR_PRIV_FLAG_LOCK_FAST;
		return (0);
	}

	/* we need to lock some io_vectors, mark this mvector as locked down */
	mvector->im_private->mp_flags |= IO_MVECTOR_PRIV_FLAG_LOCK;

	/* loop through the io_vectors to make sure they are all locked down */
	for (i = i; i < mvector->im_iovec_cnt; i++) {
		io_vector_t *iovec;

		iovec = &mvector->im_vects[i];

		/* if this io_vector has a UVA, lock it down */
		if (iovec->iv_flags & IO_VECTOR_FLAG_UVA) {
			enum seg_rw rwflag;
			offset_t offset;
			page_t **pplist;
			size_t npages;
			int rc;

			/* convert the WRITE flag for as_pagelock */
			if (iovec->iv_flags & IO_VECTOR_FLAG_WRITE)
				rwflag = S_READ;
			else
				rwflag = S_WRITE;

			/* lock down the UVA range */
			ASSERT(iovec->iv_flags & IO_VECTOR_FLAG_UVA);
			rc = as_pagelock(iovec->iv.ib_uva.iu_as, &pplist,
			    (caddr_t)iovec->iv.ib_uva.iu_addr, iovec->iv_len,
			    rwflag);
			if (rc != 0) {
				goto lockfail_aspagelock;
			}

			/* If we got back a shadow list, save it away */
			if (pplist != NULL) {
				i_io_vector_info(iovec, 0, &npages, NULL,
				    &offset);
				iovec->iv_flags |= IO_VECTOR_FLAG_SHADOW;
				iovec->iv_shadow = kmem_alloc(
				    sizeof (struct io_shadow), KM_SLEEP);
				iovec->iv_shadow->is_pgcnt = npages;
				iovec->iv_shadow->is_offset = offset;
				iovec->iv_shadow->is_pplist = pplist;
			}

			iovec->iv_flags |= IO_VECTOR_PRIV_FLAG_LOCK;
		}
	}

	return (0);

lockfail_aspagelock:
	i_io_mvector_unlock(mvector);
	return (-1);
}


/*
 * io_mvector_mapin()
 *    ensure that all io_vectors are mapped into KVA space. Any memory which
 *    is mapped will be unmapped during io_mvector_free().
 *
 *    NOTE: in general, the io_mvector_copy*() interfaces should be used over
 *    io_mvector_mapin() when feasible since unmapping KVA is very expensive
 *    and the copy routines will use kpm if available.
 */
/*ARGSUSED*/
int
io_mvector_mapin(io_mvector_t *mvector, io_mvector_mapin_flags_t flags)
{
	int vmflags;
	int i;


	/* if we've already mapped in this mvector, nothing to do */
	if (mvector->im_private->mp_flags &
	    (IO_MVECTOR_PRIV_FLAG_MAPIN | IO_MVECTOR_PRIV_FLAG_MAPIN_FAST))
		return (0);

	/* See if any of the io_vector's don't have a KVA */
	for (i = 0; i < mvector->im_iovec_cnt; i++)
		if (!(mvector->im_vects[i].iv_flags & IO_VECTOR_FLAG_KADDR))
			break;

	/*
	 * if all of the io_vectors have a KVA, we hit the fast path and we
	 * are done.
	 */
	if (i >= mvector->im_iovec_cnt) {
		mvector->im_private->mp_flags |=
		    IO_MVECTOR_PRIV_FLAG_MAPIN_FAST;
		return (0);
	}

	vmflags = flags & VM_KMFLAGS;

	/* if the mvector hasn't been locked down yet, lock it down now */
	if (!(mvector->im_private->mp_flags & (IO_MVECTOR_PRIV_FLAG_LOCK |
	    IO_MVECTOR_PRIV_FLAG_LOCK_FAST))) {
		int rc;

		/*
		 * we can only enter this path if this is a VM_SLEEP. If the
		 * caller needs to call mapin with NOSLEEP, they should call
		 * io_mvector_lock() themselves at an earlier time.
		 */
		if (vmflags & VM_NOSLEEP)
			return (-1);

		rc = io_mvector_lock(mvector);
		if (rc != 0)
			return (-1);
	}

	/*
	 * if we only want to map in so that we guarantee the mvector copy
	 * routines will always be succesfull, and kpm is enabled, nothing more
	 * to do. NOTE: this must be after we have ensured memory was locked
	 * down.
	 */
	if ((flags & IO_MVECTOR_MAPIN_FLAG_COPYONLY) && kpm_enable)
		return (0);

	/* we need to mapin some io_vectors, mark this mvector as mapped in */
	mvector->im_private->mp_flags |= IO_MVECTOR_PRIV_FLAG_MAPIN;

	for (i = i; i < mvector->im_iovec_cnt; i++) {
		io_vector_t *iovec;

		iovec = &mvector->im_vects[i];

		/* if this io_vector doesn't have a kaddr, map it in */
		if (!(iovec->iv_flags & IO_VECTOR_FLAG_KADDR)) {
			uintptr_t kaddr;
			offset_t offset;
			size_t npages;
			void *uaddr;
			page_t *pp;
			size_t j;

			/*
			 * allocate npages of KVA and adjust the KVA with the
			 * io_vector's first page offset
			 */
			i_io_vector_info(iovec, 0, &npages, NULL, &offset);
			iovec->iv_kaddr = vmem_alloc(heap_arena, ptob(npages),
			    vmflags);
			if (iovec->iv_kaddr == NULL)
				goto mapinfail_vmemalloc;
			iovec->iv_kaddr = (void *)((uintptr_t)iovec->iv_kaddr +
			    (uintptr_t)offset);

			/* we now have a KVA and the io_vector is mapped in */
			iovec->iv_flags |= IO_VECTOR_FLAG_KADDR |
			    IO_VECTOR_PRIV_FLAG_MAPIN;

			kaddr = (uintptr_t)iovec->iv_kaddr & PAGEMASK;
			if (iovec->iv_flags & IO_VECTOR_FLAG_UVA) {
				uaddr = (void *)(
				    (uintptr_t)iovec->iv.ib_uva.iu_addr &
				    (uintptr_t)PAGEMASK);
			} else {
				ASSERT(iovec->iv_flags & IO_VECTOR_FLAG_PLIST);
				pp = iovec->iv.ib_plist.ip_pp;
			}

			/*
			 * map in all the io_vector pages into the KVA we
			 * allocated.
			 */
			for (j = 0; j < npages; j++) {
				pfn_t pfnum;

				/* get the PFN, then update for next page */
				if (iovec->iv_flags & IO_VECTOR_FLAG_UVA) {
					pfnum = hat_getpfnum(
					    iovec->iv.ib_uva.iu_as->a_hat,
					    (caddr_t)uaddr);
					uaddr = (void *)((uintptr_t)uaddr +
					    PAGESIZE);
				} else {
					pfnum = pp->p_pagenum;
					pp = pp->p_next;
				}

				hat_devload(kas.a_hat, (void *)kaddr, PAGESIZE,
				    pfnum, PROT_READ | PROT_WRITE,
				    HAT_LOAD_LOCK);
				kaddr += PAGESIZE;
			}
		}
	}

	return (0);

mapinfail_vmemalloc:
	i_io_mvector_mapout(mvector);
	return (-1);
}
#endif


/*
 * io_mvector_copyout()
 *    copy 'size' bytes from a mvector (starting at 'offset' within the
 *    mvector) into a kernel buffer starting at 'buf'.
 */
int
io_mvector_copyout(io_mvector_t *mvector, void *buf, offset_t offset,
    size_t size)
{
	int rc;

	rc = i_io_mvector_copy_common(mvector, buf, offset, size,
	    i_io_copyout);

	return (rc);
}


/*
 * io_mvector_copyin()
 *    copy 'size' bytes from a kernel buffer starting at 'buf' into a mvector
 *    (starting at 'offset' within the mvector).
 */
int
io_mvector_copyin(void *buf, io_mvector_t *mvector, offset_t offset,
    size_t size)
{
	int rc;

	rc = i_io_mvector_copy_common(mvector, buf, offset, size,
	    i_io_copyin);

	return (rc);
}


/*
 * io_mvec_init()
 *    initialize io_mvector global state.
 */
void
io_mvec_init()
{
	io_mvec_state.io_mvector_cache = kmem_cache_create("mvector_cache",
	    sizeof (struct io_mvecpriv), 64, NULL, NULL, NULL, NULL, NULL, 0);
	io_list_init(&io_mvec_state.io_list);
}


/*
 * io_mvec_fini()
 *    cleanup io_mvector global state.
 */
void
io_mvec_fini()
{
	io_list_fini(&io_mvec_state.io_list);
	kmem_cache_destroy(io_mvec_state.io_mvector_cache);
}


#ifdef	_KERNEL
/*
 * io_parse_dmarobj()
 *    parse the ddi_dma_obj passed in and return the appropriate values
 *    for as, vaddr, etc.
 */
void
io_parse_dmarobj(ddi_dma_obj_t *dmar_object, uint_t iovcnt,
    struct as **asp, caddr_t *vaddr, uint32_t *size, page_t ***pplist,
    page_t **pp, uint64_t *offset)
{
	io_vector_t *io_vector;


	/*
	 * initialize the variables which aren't initialized in all code
	 * paths.
	 */
	*vaddr = NULL;
	*pplist = NULL;
	*pp = NULL;

	/* if this dma is an io mvector */
	if (dmar_object->dmao_type == DMA_OTYP_MVECTOR) {
		/* get the current io_vector that we are on */
		io_vector = &dmar_object->dmao_obj.mvector->im_vects[iovcnt];
		*size = io_vector->iv_len;

		/* the current io_vector we are on has a list of page_t's */
		if (io_vector->iv_flags & IO_VECTOR_FLAG_PLIST) {
			*pp = io_vector->iv.ib_plist.ip_pp;
			*offset = io_vector->iv.ib_plist.ip_offset;
			*asp = NULL;

		/* the current io_vector we are on has an array of page_t's */
		} else if (io_vector->iv_flags & IO_VECTOR_FLAG_SHADOW) {
			*pplist = (page_t **)io_vector->iv_shadow->is_pplist;
			*offset = io_vector->iv_shadow->is_offset;
			if (io_vector->iv_flags & IO_VECTOR_FLAG_KADDR)
				*asp = &kas;
			else
				*asp = io_vector->iv.ib_uva.iu_as;

		/* the current io_vector we are on has a kernel or user VA */
		} else {
			if (io_vector->iv_flags & IO_VECTOR_FLAG_KADDR) {
				*asp = &kas;
				*vaddr = io_vector->iv_kaddr;
			} else {
				*asp = io_vector->iv.ib_uva.iu_as;
				*vaddr = io_vector->iv.ib_uva.iu_addr;
			}
			*offset = (uintptr_t)*vaddr & PAGEOFFSET;
		}

	/* if this dma is a list of page_t's */
	} else if (dmar_object->dmao_type == DMA_OTYP_PAGES) {
		*size = dmar_object->dmao_size;
		*pp = dmar_object->dmao_obj.pp_obj.pp_pp;
		ASSERT(!PP_ISFREE(*pp) && PAGE_LOCKED(*pp));
		*offset =  dmar_object->dmao_obj.pp_obj.pp_offset & PAGEOFFSET;
		*asp = NULL;

	} else {
		ASSERT((dmar_object->dmao_type == DMA_OTYP_VADDR) ||
		    (dmar_object->dmao_type == DMA_OTYP_BUFVADDR));
		*size = dmar_object->dmao_size;

		/* this dma has an array of page_t's */
		if (dmar_object->dmao_obj.virt_obj.v_priv != NULL) {
			*pplist = dmar_object->dmao_obj.virt_obj.v_priv;
			*vaddr = dmar_object->dmao_obj.virt_obj.v_addr;
			*asp = dmar_object->dmao_obj.virt_obj.v_as;
			if (*asp == NULL)
				*asp = &kas;

		/* this dma has a kernel or user VA */
		} else {
			*vaddr = dmar_object->dmao_obj.virt_obj.v_addr;
			*asp = dmar_object->dmao_obj.virt_obj.v_as;
			if (*asp == NULL)
				*asp = &kas;
		}
		*offset = (uintptr_t)*vaddr & PAGEOFFSET;
	}
}


/*
 * io_get_page_cnt()
 *    returns the number of pages a dma obj uses from an offset into the start
 *    of the buffer and a size starting from the offset.
 */
int
io_get_page_cnt(ddi_dma_obj_t *dmar_object, offset_t offset, size_t size)
{
	uint_t poff;
	int pcnt;


	/* if size is 0, use the entire dma obj */
	if (size == 0)
		size = dmar_object->dmao_size;

	/*
	 * If this isn't an mvector, do the standard offset and page count
	 * calculations.
	 */
	if (dmar_object->dmao_type != DMA_OTYP_MVECTOR) {
		uint_t dmaoff;

		if (dmar_object->dmao_type == DMA_OTYP_PAGES)
			dmaoff = dmar_object->dmao_obj.pp_obj.pp_offset &
			    PAGEOFFSET;
		else
			dmaoff = (uint_t)(
			    (uintptr_t)dmar_object->dmao_obj.virt_obj.v_addr &
			    PAGEOFFSET);

		poff = (dmaoff + offset) & PAGEOFFSET;
		pcnt = mmu_btopr(size + poff);

	/* this is a mvector */
	} else {
		io_mvector_t *mvector;
		int i;

		mvector = dmar_object->dmao_obj.mvector;
		pcnt = 0;
		i = 0;

		/* loop while we have buffer still to look at */
		do {
			io_vector_t *iovec;

			ASSERT(i < mvector->im_iovec_cnt);
			iovec = &mvector->im_vects[i];
			i++; /* nothing below should use i */

			/* get the offset into the io_vector */
			if (iovec->iv_flags & IO_VECTOR_FLAG_KADDR) {
				poff = (uintptr_t)iovec->iv_kaddr & PAGEOFFSET;
			} else if (iovec->iv_flags & IO_VECTOR_FLAG_UVA) {
				poff = (uintptr_t)iovec->iv.ib_uva.iu_addr &
				    PAGEOFFSET;
			} else {
				ASSERT(iovec->iv_flags & IO_VECTOR_FLAG_PLIST);
				poff = iovec->iv.ib_plist.ip_offset;
			}

			/*
			 * If the io_vector is less than our starting offset,
			 * go to the next io_vector (after adjusting offset).
			 */
			if (iovec->iv_len < offset) {
				offset -= iovec->iv_len;
				continue;
			}

			/*
			 * if we have an offset into the current io_vector,
			 * use that offset when figuring out how many pages
			 * the current io_vector takes up.
			 */
			if (offset > 0) {
				pcnt += mmu_btopr(iovec->iv_len + poff -
				    offset);
				offset = 0;
			} else {
				pcnt += mmu_btopr(iovec->iv_len + poff);
			}

			if (size > iovec->iv_len)
				size -= iovec->iv_len;
			else
				size = 0;

		} while (size > 0);
	}

	return (pcnt);
}
#endif

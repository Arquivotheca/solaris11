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

#ifndef _SYS_IO_MVEC_H
#define	_SYS_IO_MVEC_H

#include <sys/types.h>
#include <sys/io_tag.h>

#ifdef	__cplusplus
extern "C" {
#endif


/*
 * Multiple IO Vectors
 *
 * An IO vector (io_vector_t) is a virtually contiguous buffer which is
 * likely to be physically discontiguous. For any given IO vector, length must
 * always be provided along with at least one of the following: kaddr, page
 * list, or a user virtual address(UVA) (page list and UVA are mutually
 * exclusive relative to each other). In addition, a kaddr may or may not be
 * present for each IO vector. For example, a UVA may have been mapped into the
 * kernel and have a KVA.
 *
 * Multiple IO vectors (io_mvector_t) are two or more (although 1 is permitted)
 * IO vectors which are virtually discontiguous and compose a single IO. The
 * IO vectors within an io_mvector_t may have mixed origins (e.g. one IO
 * vector may be a kaddr, and another a UVA).
 */

typedef struct io_plist {
	size_t		ip_offset; /* offset into first page */
	struct page	*ip_pp; /* linked list of pages */
} io_plist_t;
typedef struct io_uva {
	struct as	*iu_as; /* user as, do not use for kas */
	void		*iu_addr; /* user addr */
} io_uva_t;
typedef union io_bufinfo {
	io_plist_t	ib_plist;
	io_uva_t	ib_uva;
} io_bufinfo_t;

/* cache of pages which make up a buffer */
struct io_shadow {
	size_t  is_pgcnt; /* number of pages */
	size_t  is_offset; /* offset into first page */
	void	*is_pplist; /* opaque pplist */
};
typedef struct io_shadow *io_shadow_t;

typedef enum {
	IO_VECTOR_FLAG_KADDR	= (1 << 0), /* kaddr is present */
	IO_VECTOR_FLAG_PLIST	= (1 << 1), /* bufinfo contains a ib_plist */
	IO_VECTOR_FLAG_UVA	= (1 << 2), /* bufinfo contains a ib_uva */
	IO_VECTOR_FLAG_SHADOW	= (1 << 9), /* shadow list is present */
	IO_VECTOR_FLAG_WRITE	= (1 << 16)  /* write if set, read if not */
	/* bits 24 - 31 are reserved for private functionality */
} io_vector_flags_t;

typedef struct io_vector {
	void			*iv_kaddr; /* kernel VA */
	size_t			iv_len; /* vector length */
	io_shadow_t		iv_shadow;
	io_bufinfo_t		iv;
	io_vector_flags_t	iv_flags;
} io_vector_t;

typedef struct io_mvecpriv *io_mvecpriv_t;
typedef struct io_mvector {
	size_t		im_len; /* total length of all io vectors combined */
	size_t		im_iovec_cnt; /* number of io vectors */
	io_vector_t	*im_vects; /* array of io vectors */
	io_tags_t	im_tags;
	io_mvecpriv_t	im_private; /* implementation private data */
} io_mvector_t;

typedef enum {
	/* bits 0 - 7 are reserved for VM_KMFLAGS (sys/vmem.h) */

	/*
	 * don't mapin the mvector if we can copyin/copyout without having
	 * to mapin any of the io_vectors. if mapin is successful (with or
	 * without the IO_MVECTOR_MAPIN_FLAGS_COPYONLY flag), copyin/copyout
	 * are guaranteed to succeed.
	 */
	IO_MVECTOR_MAPIN_FLAG_COPYONLY = (1 << 16)
} io_mvector_mapin_flags_t;

io_mvector_t *io_mvector_alloc(uint_t iovec_cnt);
void io_mvector_free(io_mvector_t *mvector);
int io_mvector_mapin(io_mvector_t *mvector, io_mvector_mapin_flags_t flags);
int io_mvector_copyout(io_mvector_t *mvector, void *buf, offset_t offset,
    size_t size);
int io_mvector_copyin(void *buf, io_mvector_t *mvector, offset_t offset,
    size_t size);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IO_MVEC_H */

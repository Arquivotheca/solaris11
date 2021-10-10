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

#ifndef _SMB_AVL_H
#define	_SMB_AVL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/avl.h>

#ifdef _KERNEL
#include <sys/ksynch.h>
#include <sys/errno.h>
#else
#include <synch.h>
#include <errno.h>
#endif

/*
 * Functions provided by the consumer of smb_avl_t.
 * These functions take an object of the type that's being
 * stored in the AVL.
 *
 * avln_cmp	compare function used to build/search the AVL
 *
 * avln_add	is called before adding an item to the AVL.
 * 		This function must return 0 upon success. If it
 * 		returns a non-zero value the item is not added.
 *
 * avln_remove	is called after an item is removed from the AVL
 *
 * avln_hold	is called after any operation that looks up an
 * 		item, currently smb_avl_lookup/first/next/prev
 *
 * avln_rele	is called in smb_avl_release() which must be called
 * 		after any of the lookup operations mentioned above.
 *
 * avln_flush	is called in smb_avl_flush() for each node that's
 * 		being removed.
 */
typedef struct smb_avl_nops {
	int		(*avln_cmp)(const void *, const void *);
	int		(*avln_add)(const void *);
	void		(*avln_remove)(void *);
	void		(*avln_hold)(const void *);
	void		(*avln_rele)(const void *);
	void		(*avln_flush)(void *);
} smb_avl_nops_t;

typedef struct smb_avl {
#ifdef _KERNEL
	krwlock_t	avl_lock;
#else
	rwlock_t	avl_lock;
#endif
	avl_tree_t	avl_tree;
	smb_avl_nops_t	*avl_nops;
} smb_avl_t;

smb_avl_t *smb_avl_create(size_t, size_t, smb_avl_nops_t *);
void smb_avl_destroy(smb_avl_t *);
void smb_avl_flush(smb_avl_t *);
int smb_avl_add(smb_avl_t *, void *);
void smb_avl_remove(smb_avl_t *, void *);
void *smb_avl_lookup(smb_avl_t *, void *);
void smb_avl_release(smb_avl_t *, void *);
void *smb_avl_first(smb_avl_t *);
void *smb_avl_next(smb_avl_t *, void *);
void *smb_avl_prev(smb_avl_t *, void *);
uint32_t smb_avl_numnodes(smb_avl_t *);

#ifdef __cplusplus
}
#endif

#endif /* _SMB_AVL_H */

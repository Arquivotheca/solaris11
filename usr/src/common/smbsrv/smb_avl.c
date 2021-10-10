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

#include <smbsrv/smbinfo.h>
#include <smbsrv/smb_avl.h>

#ifndef _KERNEL
#include <stdlib.h>
#endif

static int smb_avln_add(const void *);
static void smb_avln_remove(void *);
static void smb_avln_hold(const void *);
static void smb_avln_rele(const void *);

static void *smb_avl_walk(smb_avl_t *, void *, int);

/*
 * Creates an AVL tree and initializes the given smb_avl_t
 * structure using the passed args
 */
smb_avl_t *
smb_avl_create(size_t size, size_t offset, smb_avl_nops_t *ops)
{
	smb_avl_t *avl;

	SMB_ASSERT(ops);

#ifdef _KERNEL
	avl = kmem_zalloc(sizeof (smb_avl_t), KM_NOSLEEP);
#else
	avl = calloc(1, sizeof (smb_avl_t));
	if (avl == NULL)
		return (NULL);
#endif

	SMB_RW_INIT(&avl->avl_lock);

	SMB_ASSERT(ops->avln_cmp);
	SMB_ASSERT(ops->avln_flush);

	if (ops->avln_add == NULL)
		ops->avln_add = smb_avln_add;
	if (ops->avln_remove == NULL)
		ops->avln_remove = smb_avln_remove;
	if (ops->avln_hold == NULL)
		ops->avln_hold = smb_avln_hold;
	if (ops->avln_rele == NULL)
		ops->avln_rele = smb_avln_rele;

	avl->avl_nops = ops;
	avl_create(&avl->avl_tree, ops->avln_cmp, size, offset);

	return (avl);
}

/*
 * Destroys the specified AVL tree.
 *
 * The consumer must make sure that the AVL is
 * empty and is not being accessed before calling
 * this function.
 */
void
smb_avl_destroy(smb_avl_t *avl)
{
	if (avl == NULL)
		return;

	SMB_RW_WRLOCK(&avl->avl_lock);
	avl_destroy(&avl->avl_tree);
	SMB_RW_UNLOCK(&avl->avl_lock);

	SMB_RW_DESTROY(&avl->avl_lock);

#ifdef _KERNEL
	kmem_free(avl, sizeof (smb_avl_t));
#else
	free(avl);
#endif
}

/*
 * Removes all the nodes in the given AVL.
 * After each node is removed from the tree a
 * consumer supplied function for the flush operation,
 * i.e. avln_flush is called. The consumer should
 * do anything necessary to destroy the node in avln_flush()
 *
 * Consumer must also make sure the AVL and the nodes
 * are not being accessed before calling this function.
 */
void
smb_avl_flush(smb_avl_t *avl)
{
	void *cookie = NULL;
	void *node;

	if (avl == NULL)
		return;

	SMB_RW_WRLOCK(&avl->avl_lock);

	while ((node = avl_destroy_nodes(&avl->avl_tree, &cookie)) != NULL)
		avl->avl_nops->avln_flush(node);

	SMB_RW_UNLOCK(&avl->avl_lock);
}

/*
 * Adds the given item to the AVL if it's not already there.
 *
 * A consumer supplied function, avln_add, is called
 * before attempting to add the given item. The item is
 * added if avln_add() returns successfully.
 *
 * Returns:
 *
 *	0		Success
 * 	EEXIST		The item is already in AVL
 * 	EINVAL		AVL or item pointer is NULL
 */
int
smb_avl_add(smb_avl_t *avl, void *item)
{
	avl_index_t where;
	int rc;

	if (avl == NULL || item == NULL)
		return (EINVAL);

	SMB_RW_WRLOCK(&avl->avl_lock);

	if (avl_find(&avl->avl_tree, item, &where) == NULL) {
		if ((rc = avl->avl_nops->avln_add(item)) == 0)
			avl_insert(&avl->avl_tree, item, where);
	} else {
		rc = EEXIST;
	}

	SMB_RW_UNLOCK(&avl->avl_lock);

	return (rc);
}

/*
 * Removes the given item from the AVL.
 *
 * A consumer provided function, avln_remove, is called
 * after the item is removed from the AVL.
 */
void
smb_avl_remove(smb_avl_t *avl, void *item)
{
	avl_index_t where;
	void *node;

	if (avl == NULL || item == NULL)
		return;

	SMB_RW_WRLOCK(&avl->avl_lock);

	if ((node = avl_find(&avl->avl_tree, item, &where)) != NULL) {
		avl_remove(&avl->avl_tree, node);
		avl->avl_nops->avln_remove(node);
	}

	SMB_RW_UNLOCK(&avl->avl_lock);
}

/*
 * Looks up the AVL for the given item.
 *
 * If the item is found and the consumer has specified
 * a hold function then a hold on the object is taken before
 * the pointer to it is returned to the caller.
 *
 * The caller must call smb_avl_release() after it's done
 * using the returned object to release the hold.
 */
void *
smb_avl_lookup(smb_avl_t *avl, void *item)
{
	void *node = NULL;

	if (avl == NULL || item == NULL)
		return (NULL);

	SMB_RW_RDLOCK(&avl->avl_lock);

	if ((node = avl_find(&avl->avl_tree, item, NULL)) != NULL)
		avl->avl_nops->avln_hold(node);

	SMB_RW_UNLOCK(&avl->avl_lock);

	return (node);
}

/*
 * The hold on the given object is released.
 *
 * This function MUST always be called after
 * any function that returns a pointer to a
 * stored object, currently:
 *
 *  smb_avl_lookup
 *  smb_avl_first
 *  smb_avl_next
 *  smb_avl_prev
 */
void
smb_avl_release(smb_avl_t *avl, void *item)
{
	if (avl == NULL || item == NULL)
		return;

	avl->avl_nops->avln_rele(item);
}

/*
 * Returns the first (lowest value) node of the given tree,
 * or NULL if the tree is empty
 */
void *
smb_avl_first(smb_avl_t *avl)
{
	void *node;

	if (avl == NULL)
		return (NULL);

	SMB_RW_RDLOCK(&avl->avl_lock);

	if ((node = avl_first(&avl->avl_tree)) != NULL)
		avl->avl_nops->avln_hold(node);

	SMB_RW_UNLOCK(&avl->avl_lock);

	return (node);
}

void *
smb_avl_next(smb_avl_t *avl, void *oldnode)
{
	return (smb_avl_walk(avl, oldnode, AVL_AFTER));
}

void *
smb_avl_prev(smb_avl_t *avl, void *oldnode)
{
	return (smb_avl_walk(avl, oldnode, AVL_BEFORE));
}

/*
 * Returns the next (direction == AVL_AFTER) or the previous
 * (direction == AVL_BEFORE) node of the given 'oldnode'.
 *
 * 'oldnode' must be a valid node in the tree.
 */
static void *
smb_avl_walk(smb_avl_t *avl, void *oldnode, int direction)
{
	void *node;

	if (avl == NULL)
		return (NULL);

	SMB_RW_RDLOCK(&avl->avl_lock);

	if ((node = avl_walk(&avl->avl_tree, oldnode, direction)) != NULL)
		avl->avl_nops->avln_hold(node);

	SMB_RW_UNLOCK(&avl->avl_lock);

	return (node);
}

/*
 * Returns the number of nodes in the given AVL
 */
uint32_t
smb_avl_numnodes(smb_avl_t *avl)
{
	uint32_t num;

	if (avl == NULL)
		return (0);

	SMB_RW_RDLOCK(&avl->avl_lock);
	num = (uint32_t)avl_numnodes(&avl->avl_tree);
	SMB_RW_UNLOCK(&avl->avl_lock);

	return (num);
}

/*
 * This function is used whenever the consumer
 * doesn't supply any function for avln_add operation.
 */
/*ARGSUSED*/
static int
smb_avln_add(const void *p)
{
	return (0);
}

/*
 * This function is used whenever the consumer
 * doesn't supply any function for avln_remove operation.
 */
/*ARGSUSED*/
static void
smb_avln_remove(void *p)
{
}

/*
 * This function is used whenever the consumer
 * doesn't supply any function for avln_hold operation.
 */
/*ARGSUSED*/
static void
smb_avln_hold(const void *p)
{
}

/*
 * This function is used whenever the consumer
 * doesn't supply any function for avln_rele operation.
 */
/*ARGSUSED*/
static void
smb_avln_rele(const void *p)
{
}

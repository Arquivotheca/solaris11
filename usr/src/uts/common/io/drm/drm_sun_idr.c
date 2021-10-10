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

#include <sys/kmem.h>
#include "drmP.h"
#include "drm_linux_list.h"
#include "drm_sun_idr.h"

static inline int
fr_isfull(struct idr_free_id_range *range)
{
	return range->min_unused_id >= range->end;
}

static struct idr_free_id_range *
fr_new(int start)
{
	struct idr_free_id_range *this;

	this = kmem_zalloc(sizeof(struct idr_free_id_range), KM_SLEEP);
	this->start = start;
	this->end = 0x7fffffff;
	this->min_unused_id = start;

	return this;
}

static struct idr_free_id_range *
fr_destroy(struct idr_free_id_range *range)
{
	struct idr_free_id_range *ret;
	struct idr_free_id *id;

	while (range->free_ids) {
		id = range->free_ids;
		range->free_ids = range->free_ids->next;
		kmem_free(id, sizeof(struct idr_free_id));
	}

	ret = range->next;
	kmem_free(range, sizeof(struct idr_free_id_range));

	return ret;
}

static struct idr_free_id_range *
fr_get(struct idr_free_id_range *list, int start)
{
	struct idr_free_id_range *entry = list;

	while (entry && (entry->start != start))
		entry = entry->next;
	return entry;
}

static struct idr_free_id_range *
fr_insert(struct idr_free_id_range *list, int start)
{
	struct idr_free_id_range *prev = list;
	struct idr_free_id_range *n;
	struct idr_free_id *id, **pid;

	while (prev->next && prev->next->start < start)
		prev = prev->next;

	n = fr_new(start);

	/* link the new range */
	n->next = prev->next;
	prev->next = n;

	/* change the end of the ranges */
	prev->end = start;
	if (n->next)
		n->end = n->next->start;

	if (fr_isfull(prev)) {
		/* change the min_unused_id of the ranges */
		n->min_unused_id = prev->min_unused_id;
		prev->min_unused_id = start;

		/* link free id to the new range */
		pid = &prev->free_ids;
		while (*pid) {
			if ((*pid)->id < start) {
				pid = &((*pid)->next);
				continue;
			}
			id = *pid;

			/* remove from the prev range */
			(*pid) = id->next;

			/* link to the new range */
			id->next = n->free_ids;
			n->free_ids = id;
		}
	}

	return n;
}

static int
idr_compare(const void *a, const void *b)
{
	const struct idr_used_id *pa = a;
	const struct idr_used_id *pb = b;

	if (pa->id < pb->id)
		return (-1);
	else if (pa->id > pb->id)
		return (1);
	else
		return (0);
}

static int
idr_get_free_id_in_range(struct idr_free_id_range* range)
{
	struct idr_free_id *idp;
	int id;

	if (range->free_ids) {
		idp = range->free_ids;
		range->free_ids = idp->next;
		id = idp->id;
		kmem_free(idp, sizeof(struct idr_free_id));
		return (id);
	}

	if (!fr_isfull(range)) {
		id = range->min_unused_id;
		range->min_unused_id++;
		return (id);
	}

	return (-1);
}

void
idr_init(struct idr *idrp)
{
	avl_create(&idrp->used_ids, idr_compare, sizeof(struct idr_used_id),
	    offsetof(struct idr_used_id, link));

	idrp->free_id_ranges = fr_new(0);
	mutex_init(&idrp->lock, NULL, MUTEX_DRIVER, NULL);
}

int
idr_get_new_above(struct idr *idrp, void *obj, int start, int *newid)
{
	struct idr_used_id *used;
	struct idr_free_id_range *range;
	int id;

	if (start < 0)
		return (-EINVAL);
	mutex_enter(&idrp->lock);
	range = fr_get(idrp->free_id_ranges, start);
	if (!range)
		range = fr_insert(idrp->free_id_ranges, start);

	while (range) {
		id = idr_get_free_id_in_range(range);
		if (id >= 0)
			goto got_id;
		range = range->next;
	}
	mutex_exit(&idrp->lock);
	return (-1);

got_id:
	used = kmem_alloc(sizeof(struct idr_used_id), KM_NOSLEEP);
	if (!used) {
		mutex_exit(&idrp->lock);
		return (-ENOMEM);
	}

	used->id = id;
	used->obj = obj;
	avl_add(&idrp->used_ids, used);

	*newid = id;
	mutex_exit(&idrp->lock);
	return (0);
}

static struct idr_used_id *
idr_find_used_id(struct idr *idrp, uint32_t id)
{
	struct idr_used_id match;
	struct idr_used_id *ret;

	match.id = id;

	ret = avl_find(&idrp->used_ids, &match, NULL);
	if (ret) {
		return (ret);
	}

	return (NULL);
}

void *
idr_find(struct idr *idrp, uint32_t id)
{
	struct idr_used_id *ret;

	mutex_enter(&idrp->lock);
	ret = idr_find_used_id(idrp, id);
	if (ret) {
		mutex_exit(&idrp->lock);
		return (ret->obj);
	}

	mutex_exit(&idrp->lock);
	return (NULL);
}

int
idr_remove(struct idr *idrp, uint32_t id)
{
	struct idr_free_id_range *range;
	struct idr_used_id *ide;
	struct idr_free_id *fid;

	mutex_enter(&idrp->lock);
	ide = idr_find_used_id(idrp, id);
	if (!ide) {
		mutex_exit(&idrp->lock);
		return (-EINVAL);
	}

	fid = kmem_alloc(sizeof(struct idr_free_id), KM_NOSLEEP);
	if (!fid) {
		mutex_exit(&idrp->lock);
		return (-ENOMEM);
	}
	fid->id = id;


	range = idrp->free_id_ranges;
	while (range->end <= id)
		range = range->next;
	fid->next = range->free_ids;
	range->free_ids = fid;
	avl_remove(&idrp->used_ids, ide);
	kmem_free(ide, sizeof (struct idr_used_id));
	mutex_exit(&idrp->lock);

	return (0);
}

void 
idr_remove_all(struct idr *idrp)
{
	idr_destroy(idrp);
	idr_init(idrp);
}

void *
idr_replace(struct idr *idrp, void *obj, uint32_t id)
{
	struct idr_used_id *ide;
	void *ret;
	mutex_enter(&idrp->lock);
	ide = idr_find_used_id(idrp, id);
	if (!ide) {
		mutex_exit(&idrp->lock);
		return (void*)(-EINVAL);
	}

	ret = ide->obj;
	ide->obj = obj;
	mutex_exit(&idrp->lock);
	return ret;
}

int
idr_for_each(struct idr *idrp, int (*fn)(int id, void *p, void *data), void *data)
{
	struct idr_used_id *ide;
	int ret;

	ide = avl_first(&idrp->used_ids);
	while (ide) {
		ret = fn(ide->id, ide->obj, data);
		if (ret)
			break;

		ide = AVL_NEXT(&idrp->used_ids, ide);
	}

	return ret;
}

int
/* LINTED */
idr_pre_get(struct idr *idrp, int flag) {
	return (-1);
}

void
idr_destroy(struct idr *idrp)
{
	struct idr_free_id_range *range;
	struct idr_used_id *ide;
	void *cookie = NULL;

	while (ide = avl_destroy_nodes(&idrp->used_ids, &cookie))
		kmem_free(ide, sizeof (struct idr_used_id));
	avl_destroy(&idrp->used_ids);

	range = idrp->free_id_ranges;
	while (range)
		range = fr_destroy(range);
	idrp->free_id_ranges = NULL;

	mutex_destroy(&idrp->lock);
}

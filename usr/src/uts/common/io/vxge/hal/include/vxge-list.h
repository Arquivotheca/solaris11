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

/*
 * Copyright Exar 2010. Copyright (c) 2002-2010 Neterion, Inc.
 * All right Reserved.
 *
 * FileName :   vxge-list.h
 *
 * Description:  Generic bi-directional linked list implementation
 *
 * Created:       27 December 2006
 */

#ifndef	VXGE_LIST_H
#define	VXGE_LIST_H

__EXTERN_BEGIN_DECLS

/*
 * struct vxge_list_t - List item.
 * @prev: Previous list item.
 * @next: Next list item.
 *
 * Item of a bi-directional linked list.
 */
typedef struct vxge_list_t {
	struct vxge_list_t		 *prev;
	struct vxge_list_t		 *next;
} vxge_list_t;

/*
 * vxge_list_init - Initialize linked list.
 * @header: first element of the list (head)
 *
 * Initialize linked list.
 * See also: vxge_list_t {}.
 */
void vxge_list_init(vxge_list_t *header);
#pragma inline(vxge_list_init)

/*
 * vxge_list_is_empty - Is the list empty?
 * @header: first element of the list (head)
 *
 * Determine whether the bi-directional list is empty. Return '1' in
 * case of 'empty'.
 * See also: vxge_list_t {}.
 */
int vxge_list_is_empty(vxge_list_t *header);
#pragma inline(vxge_list_is_empty)

/*
 * vxge_list_first_get - Return the first item from the linked list.
 * @header: first element of the list (head)
 *
 * Returns the next item from the header.
 * Returns NULL if the next item is header itself
 * See also: vxge_list_remove(), vxge_list_insert(), vxge_list_t {}.
 */
vxge_list_t *vxge_list_first_get(vxge_list_t *header);
#pragma inline(vxge_list_first_get)

/*
 * vxge_list_remove - Remove the specified item from the linked list.
 * @item: element of the list
 *
 * Remove item from a list.
 * See also: vxge_list_insert(), vxge_list_t {}.
 */
void vxge_list_remove(vxge_list_t *item);
#pragma inline(vxge_list_remove)

/*
 * vxge_list_insert - Insert a new item after the specified item.
 * @new_item: new element of the list
 * @prev_item: element of the list after which the new element is
 *		inserted
 *
 * Insert new item (new_item) after given item (prev_item).
 * See also: vxge_list_remove(), vxge_list_insert_before(), vxge_list_t {}.
 */
void vxge_list_insert(vxge_list_t *new_item,
				    vxge_list_t *prev_item);
#pragma inline(vxge_list_insert)

/*
 * vxge_list_insert_before - Insert a new item before the specified item.
 * @new_item: new element of the list
 * @next_item: element of the list after which the new element is inserted
 *
 * Insert new item (new_item) before given item (next_item).
 */
void vxge_list_insert_before(vxge_list_t *new_item,
					    vxge_list_t *next_item);
#pragma inline(vxge_list_insert_before)

#define	vxge_list_for_each(_p, _h) \
	for (_p = (_h)->next, vxge_os_prefetch(_p->next); _p != (_h); \
		_p = _p->next, vxge_os_prefetch(_p->next))

#define	vxge_list_for_each_safe(_p, _n, _h) \
	for (_p = (_h)->next, _n = _p->next; _p != (_h); \
		_p = _n, _n = _p->next)

#define	vxge_list_for_each_prev_safe(_p, _n, _h) \
	for (_p = (_h)->prev, _n = _p->prev; _p != (_h); \
		_p = _n, _n = _p->prev)

#ifdef	__GNUC__
/*
 * vxge_container_of - Given a member, return the containing structure.
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 * Cast a member of a structure out to the containing structure.
 */
#define	vxge_container_of(ptr, type, member) (\
	{ __typeof(((type *)0)->member) *__mptr = (ptr);	\
	(type *)(void *)((char *)__mptr - ((ptr_t)&((type *)0)->member)); })
#else
/* type unsafe version */
#define	vxge_container_of(ptr, type, member) \
	((type *)(void *)((char *)(ptr) - ((ptr_t)&((type *)0)->member)))
#endif

/*
 * vxge_offsetof - Offset of the member in the containing structure.
 * @t:	struct name.
 * @m:	the name of the member within the struct.
 *
 * Return the offset of the member @m in the structure @t.
 */
#define	vxge_offsetof(t, m)			((ptr_t)(&((t *)0)->m))

__EXTERN_END_DECLS

#endif /* VXGE_LIST_H */

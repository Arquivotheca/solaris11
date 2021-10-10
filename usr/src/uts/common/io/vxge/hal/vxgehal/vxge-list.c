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
 * FileName :   vxge-list.c
 *
 * Description:  list operations function definition
 *
 * Created:       7 June 2004
 */

#include "vxgehal.h"
#include <sys/strsun.h>


/*
 * vxge_list_init - Initialize linked list.
 * @header: first element of the list (head)
 *
 * Initialize linked list.
 * See also: vxge_list_t {}.
 */
void
vxge_list_init(vxge_list_t *header)
{
	vxge_assert(header != NULL);

	header->next = header;
	header->prev = header;
}

/*
 * vxge_list_is_empty - Is the list empty?
 * @header: first element of the list (head)
 *
 * Determine whether the bi-directional list is empty. Return '1' in
 * case of 'empty'.
 * See also: vxge_list_t {}.
 */
int
vxge_list_is_empty(vxge_list_t *header)
{
	vxge_assert(header != NULL);
	return (header->next == header);
}

/*
 * vxge_list_first_get - Return the first item from the linked list.
 * @header: first element of the list (head)
 *
 * Returns the next item from the header.
 * Returns NULL if the next item is header itself
 * See also: vxge_list_remove(), vxge_list_insert(), vxge_list_t {}.
 */

vxge_list_t
*vxge_list_first_get(vxge_list_t *header)
{
	vxge_assert(header != NULL);
	vxge_assert(header->next != NULL);
	vxge_assert(header->prev != NULL);

	if (header->next == header)
		return (NULL);
	else
		return (header->next);
}

/*
 * vxge_list_remove - Remove the specified item from the linked list.
 * @item: element of the list
 *
 * Remove item from a list.
 * See also: vxge_list_insert(), vxge_list_t {}.
 */
void
vxge_list_remove(vxge_list_t *item)
{
	vxge_assert(item != NULL);
	vxge_assert(item->next != NULL);
	vxge_assert(item->prev != NULL);

	item->next->prev = item->prev;
	item->prev->next = item->next;
#ifdef  VXGE_DEBUG_ASSERT
	item->next = item->prev = NULL;
#endif
}

/*
 * vxge_list_insert - Insert a new item after the specified item.
 * @new_item: new element of the list
 * @prev_item: element of the list after which the new element is
 *              inserted
 *
 * Insert new item (new_item) after given item (prev_item).
 * See also: vxge_list_remove(), vxge_list_insert_before(), vxge_list_t {}.
 */
void vxge_list_insert(vxge_list_t *new_item,
	vxge_list_t *prev_item)
{
	vxge_assert(new_item  != NULL);
	vxge_assert(prev_item != NULL);
	vxge_assert(prev_item->next != NULL);

	new_item->next = prev_item->next;
	new_item->prev = prev_item;
	prev_item->next->prev = new_item;
	prev_item->next = new_item;
}

/*
 * vxge_list_insert_before - Insert a new item before the specified item.
 * @new_item: new element of the list
 * @next_item: element of the list after which the new element is inserted
 *
 * Insert new item (new_item) before given item (next_item).
 */
void vxge_list_insert_before(vxge_list_t *new_item,
	vxge_list_t *next_item)
{
	vxge_assert(new_item  != NULL);
	vxge_assert(next_item != NULL);
	vxge_assert(next_item->next != NULL);

	new_item->next = next_item;
	new_item->prev = next_item->prev;
	next_item->prev->next = new_item;
	next_item->prev = new_item;
}

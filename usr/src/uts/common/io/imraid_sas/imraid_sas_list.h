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
 * imraid_sas_list.h: header for imraid_sas
 *
 * MegaRAID HBA driver for FALCON SAS2.0 controllers
 * Copyright (c) 2008-2010, LSI Corporation.
 * All rights reserved.
 */

#ifndef	_IMRAID_SAS_LIST_H_
#define	_IMRAID_SAS_LIST_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Simple doubly linked list implementation.
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole lists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 */

struct mlist_head {
	struct mlist_head *next, *prev;
};

typedef struct mlist_head mlist_t;

#define	LIST_HEAD_INIT(name) { &(name), &(name) }

#define	LIST_HEAD(name) \
	struct mlist_head name = LIST_HEAD_INIT(name)

#define	INIT_LIST_HEAD(ptr) { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
}


/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static void __list_add(struct mlist_head *new,
	struct mlist_head *prev,
	struct mlist_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}


/*
 * mlist_add - add a new entry
 * @new: new entry to be added
 * @head: list head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static void mlist_add(struct mlist_head *new, struct mlist_head *head)
{
	__list_add(new, head, head->next);
}


/*
 * mlist_add_tail - add a new entry
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static void mlist_add_tail(struct mlist_head *new, struct mlist_head *head)
{
	__list_add(new, head->prev, head);
}



/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static void __list_del(struct mlist_head *prev,
			struct mlist_head *next)
{
	next->prev = prev;
	prev->next = next;
}


#if 0
/*
 * mlist_del - deletes entry from list.
 * @entry:	the element to delete from the list.
 * Note:	list_empty on entry does not return true after this, the entry
 * is in an undefined state.
 */

static void mlist_del(struct mlist_head *entry)
{
	__list_del(entry->prev, entry->next);
	entry->next = entry->prev = 0;
}
#endif

/*
 * mlist_del_init - deletes entry from list and reinitialize it.
 * @entry: the element to delete from the list.
 */
static void mlist_del_init(struct mlist_head *entry)
{
	__list_del(entry->prev, entry->next);
	INIT_LIST_HEAD(entry);
}


/*
 * mlist_empty - tests whether a list is empty
 * @head: the list to test.
 */
static int mlist_empty(struct mlist_head *head)
{
	return (head->next == head);
}


/*
 * mlist_splice - join two lists
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static void mlist_splice(struct mlist_head *list, struct mlist_head *head)
{
	struct mlist_head *first = list->next;

	if (first != list) {
		struct mlist_head *last = list->prev;
		struct mlist_head *at = head->next;

		first->prev = head;
		head->next = first;

		last->next = at;
		at->prev = last;
	}
}



/* TODO: set this */
#if 0
#pragma	inline(list_add, list_add_tail, __list_del, list_del,
		list_del_init, list_empty, list_splice)
#endif


/*
 * mlist_entry - get the struct for this entry
 * @ptr:	the &struct mlist_head pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_struct within the struct.
 */
#define	mlist_entry(ptr, type, member) \
	((type *)((size_t)(ptr) - offsetof(type, member)))


/*
 * mlist_for_each	-	iterate over a list
 * @pos:	the &struct mlist_head to use as a loop counter.
 * @head:	the head for your list.
 */
#define	mlist_for_each(pos, head) \
	for (pos = (head)->next, prefetch(pos->next); pos != (head); \
		pos = pos->next, prefetch(pos->next))


/*
 * mlist_for_each_safe - iterate over a list safe against removal of list entry
 * @pos:	the &struct mlist_head to use as a loop counter.
 * @n:		another &struct mlist_head to use as temporary storage
 * @head:	the head for your list.
 */
#define	mlist_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

#ifdef __cplusplus
}
#endif

#endif /* _IMRAID_SAS_LIST_H_ */

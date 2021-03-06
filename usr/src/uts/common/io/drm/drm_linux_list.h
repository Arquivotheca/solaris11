/* BEGIN CSTYLED */

/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * drm_linux_list.h -- linux list functions for the BSDs.
 * Created: Mon Apr 7 14:30:16 1999 by anholt@FreeBSD.org
 */
/*
 * -
 * Copyright 2003 Eric Anholt
 * Copyright (c) 2009, Intel Corporation.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <anholt@FreeBSD.org>
 *
 */

#ifndef _DRM_LINUX_LIST_H_
#define	_DRM_LINUX_LIST_H_

#include <sys/types.h>
#include <sys/param.h>
struct list_head {
	struct list_head *next, *prev;
	caddr_t contain_ptr;
};

/* Cheat, assume the list_head is at the start of the struct */
#define container_of(ptr, type, member)				\
	((type *)(uintptr_t)((char *)(ptr) - (unsigned long)(&((type *)0)->member)))

#define	list_entry(ptr, type, member)				\
	((type *)(uintptr_t)(ptr->contain_ptr))

#define list_first_entry(ptr, type, member)			\
	list_entry((ptr)->next, type, member)

#define	list_empty(head)					\
	((head)->next == head)

#define	INIT_LIST_HEAD(head)					\
do { 								\
	(head)->next = head;					\
	(head)->prev = head;					\
	(head)->contain_ptr = NULL;				\
} while (*"\0")

#define	list_add(ptr, head, entry)				\
do {								\
	(ptr)->prev = head;					\
	(ptr)->next = (head)->next;				\
	(head)->next->prev = ptr;				\
	(head)->next = ptr;					\
	(ptr)->contain_ptr = entry;				\
} while (*"\0")

#define	list_add_tail(ptr, head, entry)				\
do {								\
	(ptr)->prev = (head)->prev;				\
	(ptr)->next = head;					\
	(head)->prev->next = ptr;				\
	(head)->prev = ptr;					\
	(ptr)->contain_ptr = entry;				\
} while (*"\0")

#define	list_del(ptr)						\
do {								\
	(ptr)->next->prev = (ptr)->prev;			\
	(ptr)->prev->next = (ptr)->next;			\
} while (*"\0")

#define	list_del_init(ptr)					\
do {								\
	list_del(ptr);						\
	INIT_LIST_HEAD(ptr);					\
} while (*"\0")

#define	list_move_tail(ptr, head, entry)			\
do {								\
	list_del(ptr);						\
	list_add_tail(ptr, head, entry);			\
} while (*"\0")

#define	list_for_each(pos, head)				\
	for (pos = (head)->next; pos != head; pos = (pos)->next)

#define	list_for_each_safe(pos, n, head)			\
	for (pos = (head)->next, n = (pos)->next;		\
	    pos != head; 					\
	    pos = n, n = n->next)

#define list_for_each_entry(pos, type, head, member)		\
	for (pos = list_entry((head)->next, type, member); pos; \
	    pos = list_entry(pos->member.next, type, member))

#define list_for_each_entry_safe(pos, n, type, head, member)	\
	for (pos = list_entry((head)->next, type, member),	\
	    n = pos ? list_entry(pos->member.next, type, member) : pos;	\
	    pos;						\
	    pos = n,						\
	    n = list_entry((n ? n->member.next : (head)->next), type, member))

#endif /* _DRM_LINUX_LIST_H_ */

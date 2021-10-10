/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Embedded Linked Lists
 *
 * Simple doubly-linked list implementation.  This implementation assumes that
 * each list element contains an embedded mtst_list_t (previous and next
 * pointers), which is typically the first member of the element struct.
 * An additional mtst_list_t is used to store the head (l_next) and tail
 * (l_prev) pointers.  The current head and tail list elements have their
 * previous and next pointers set to NULL, respectively.
 */

#include <sys/types.h>

#include <mtst_list.h>

void
mtst_list_append(mtst_list_t *lp, void *new)
{
	mtst_list_t *p = lp->l_prev;	/* p = tail list element */
	mtst_list_t *q = new;		/* q = new list element */

	lp->l_prev = q;
	q->l_prev = p;
	q->l_next = NULL;

	if (p != NULL)
		p->l_next = q;
	else
		lp->l_next = q;
}

void
mtst_list_prepend(mtst_list_t *lp, void *new)
{
	mtst_list_t *p = new;		/* p = new list element */
	mtst_list_t *q = lp->l_next;	/* q = head list element */

	lp->l_next = p;
	p->l_prev = NULL;
	p->l_next = q;

	if (q != NULL)
		q->l_prev = p;
	else
		lp->l_prev = p;
}

void
mtst_list_insert_before(mtst_list_t *lp, void *before_me, void *new)
{
	mtst_list_t *p = before_me;
	mtst_list_t *q = new;

	if (p == NULL || p->l_prev == NULL) {
		mtst_list_prepend(lp, new);
		return;
	}

	q->l_prev = p->l_prev;
	q->l_next = p;
	p->l_prev = q;
	q->l_prev->l_next = q;
}

void
mtst_list_insert_after(mtst_list_t *lp, void *after_me, void *new)
{
	mtst_list_t *p = after_me;
	mtst_list_t *q = new;

	if (p == NULL || p->l_next == NULL) {
		mtst_list_append(lp, new);
		return;
	}

	q->l_next = p->l_next;
	q->l_prev = p;
	p->l_next = q;
	q->l_next->l_prev = q;
}

void
mtst_list_delete(mtst_list_t *lp, void *existing)
{
	mtst_list_t *p = existing;

	if (p->l_prev != NULL)
		p->l_prev->l_next = p->l_next;
	else
		lp->l_next = p->l_next;

	if (p->l_next != NULL)
		p->l_next->l_prev = p->l_prev;
	else
		lp->l_prev = p->l_prev;
}

/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MTST_LIST_H
#define	_MTST_LIST_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mtst_list {
	struct mtst_list *l_prev;
	struct mtst_list *l_next;
} mtst_list_t;

#define	mtst_list_prev(elem)	((void *)(((mtst_list_t *)(elem))->l_prev))
#define	mtst_list_next(elem)	((void *)(((mtst_list_t *)(elem))->l_next))

extern void mtst_list_append(mtst_list_t *, void *);
extern void mtst_list_prepend(mtst_list_t *, void *);
extern void mtst_list_insert_before(mtst_list_t *, void *, void *);
extern void mtst_list_insert_after(mtst_list_t *, void *, void *);
extern void mtst_list_delete(mtst_list_t *, void *);

#ifdef __cplusplus
}
#endif

#endif /* _MTST_LIST_H */

/*
 * Copyright 1996-2002 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdlib.h>
#include "locdef.h"

#define	_COMPRESS_THRESHOLD	256

struct _from_to_tbl {
	wchar_t	from;
	wchar_t	to;
	struct _from_to_tbl	*next;
};

/*
 * FUNCTION: compress_transtabs
 *
 * DESCRIPTION:
 * Compress the transformation table and build
 * _LC_transtabs_t object
 */
void
compress_transtabs(_LC_ctype_t *ctype, int idx)
{
	_LC_transnm_t	*transnm;
	_LC_transtabs_t	*transtabs;
	wchar_t	*tbl;
	wchar_t	*tt;
	wchar_t	tmin, tmax;
	wchar_t	this_to, next_from;
	int	i, no_of_tbls;
	struct _from_to_tbl	from_to_tbl, *p, *q;

	transtabs = ctype->transtabs + idx;
	transnm = ctype->transname + idx;
	tbl = transtabs->table;
	tmin = transtabs->tmin;
	tmax = transtabs->tmax;

	from_to_tbl.next = NULL;

	/* if idx == 0 or idx == 1, now compressing toupper/tolower */
	/* table */
	if ((idx == 0) || (idx == 1)) {
		if (tmax == 255) {
			/* minimum tmax for toupper/tolower is 255 */
			transnm->tmin = tmin;
			transnm->tmax = tmax; /* 255 */
			tt = MALLOC(wchar_t, tmax - tmin + 1);
			(void) memcpy((void *)tt, &tbl[tmin],
			    sizeof (wchar_t) * (tmax - tmin + 1));
			transtabs->table = tt;
			transtabs->next = NULL;
			free(tbl);
			return;
		}
		/* tmax is larger than 255 */
		/* toupper/tolower table may be splitted into */
		/* multiple tables */
		p = MALLOC(struct _from_to_tbl, 1);
		from_to_tbl.next = p;
		p->from = tmin;
		p->to = 255;
		p->next = NULL;
		i = 256;
	} else {
		i = tmin;
		p = NULL;
	}

	while (tbl[i] == i) {
		/* skip identical entries */
		i++;
		if (i > tmax) {
			if (from_to_tbl.next == NULL) {
				/* no actual transformation entry found */
				/* this shouldn't happen, but */
				/* so far, leave it as is. */
				p = MALLOC(struct _from_to_tbl, 1);
				from_to_tbl.next = p;
				p->from = tmin;
				p->to = tmax;
				p->next = NULL;
			}
			goto out_of_loop;
		}
	}

	q = MALLOC(struct _from_to_tbl, 1);
	if (p) {
		p->next = q;
		p = p->next;
	} else {
		p = q;
		from_to_tbl.next = p;
	}
	p->from = i;
	i++;
	for (;;) {
		while (tbl[i] != i) {
			/* skip non-identical entries */
			i++;
			if (i > tmax) {
				/* reached to tmax */
				p->to = tmax;
				p->next = NULL;
				goto out_of_loop;
			}
		}
		p->to = i - 1;

		while (tbl[i] == i) {
			/* skip identical entries */
			i++;
			if (i > tmax) {
				/* reached to tmax */
				/* no more actual transformation entries */
				p->next = NULL;
				goto out_of_loop;
			}
		}

		p->next = MALLOC(struct _from_to_tbl, 1);
		p = p->next;
		p->from = i;
	}

out_of_loop:
	p = from_to_tbl.next;

	no_of_tbls = 1;
	while ((q = p->next) != NULL) {
		this_to = p->to;
		next_from = q->from;
		if ((next_from - this_to) < _COMPRESS_THRESHOLD) {
			/* consolidate two splitted tables */
			p->to = q->to;
			p->next = q->next;
			free(q);
		} else {
			/* gap between two splitted tables is */
			/* enough large to keep them separated */
			no_of_tbls++;
			p = q;
		}
	}

	p = from_to_tbl.next;
	transtabs->tmin = p->from;
	transtabs->tmax = p->to;
	tt = MALLOC(wchar_t, p->to - p->from + 1);
	(void) memcpy((void *)tt, &tbl[p->from],
	    sizeof (wchar_t) * (p->to - p->from + 1));
	transtabs->table = tt;
	transtabs->next = NULL;
	transnm->tmin = p->from;
	q = p;
	p = p->next;
	free(q);
	for (i = 1; i < no_of_tbls; i++) {
		transtabs->next = MALLOC(_LC_transtabs_t, 1);
		transtabs = transtabs->next;
		transtabs->tmin = p->from;
		transtabs->tmax = p->to;
		tt = MALLOC(wchar_t, p->to - p->from + 1);
		(void) memcpy((void *)tt, &tbl[p->from],
		    sizeof (wchar_t) * (p->to - p->from + 1));
		transtabs->table = tt;
		transtabs->next = NULL;
		q = p;
		p = p->next;
		free(q);
	}
	transnm->tmax = transtabs->tmax;
	free(tbl);
}

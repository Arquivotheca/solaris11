/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 1996, 1997, 1998
 *	Sleepycat Software.  All rights reserved.
 */
/*
 * Copyright (c) 1990, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 */
/*
 * Copyright (c) 1990, 1993, 1994, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Olson.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "@(#)bt_search.c	10.25 (Sleepycat) 12/16/98";
#endif /* not lint */

#ifndef NO_SYSTEM_INCLUDES
#include <sys/types.h>

#include <errno.h>
#include <string.h>
#endif

#include "db_int.h"
#include "db_page.h"
#include "btree.h"

/*
 * __bam_search --
 *	Search a btree for a key.
 *
 * PUBLIC: int __bam_search __P((DBC *,
 * PUBLIC:     const DBT *, u_int32_t, int, db_recno_t *, int *));
 */
int
__bam_search(dbc, key, flags, stop, recnop, exactp)
	DBC *dbc;
	const DBT *key;
	u_int32_t flags;
	int stop, *exactp;
	db_recno_t *recnop;
{
	BTREE *t;
	CURSOR *cp;
	DB *dbp;
	DB_LOCK lock;
	PAGE *h;
	db_indx_t base, i, indx, lim;
	db_pgno_t pg;
	db_recno_t recno;
	int cmp, jump, ret, stack;

	dbp = dbc->dbp;
	cp = dbc->internal;
	t = dbp->internal;
	recno = 0;

	BT_STK_CLR(cp);

	/*
	 * There are several ways we search a btree tree.  The flags argument
	 * specifies if we're acquiring read or write locks, if we position
	 * to the first or last item in a set of duplicates, if we return
	 * deleted items, and if we are locking pairs of pages.  In addition,
	 * if we're modifying record numbers, we have to lock the entire tree
	 * regardless.  See btree.h for more details.
	 *
	 * If write-locking pages, we need to know whether or not to acquire a
	 * write lock on a page before getting it.  This depends on how deep it
	 * is in tree, which we don't know until we acquire the root page.  So,
	 * if we need to lock the root page we may have to upgrade it later,
	 * because we won't get the correct lock initially.
	 *
	 * Retrieve the root page.
	 */
	pg = PGNO_ROOT;
	stack = F_ISSET(dbp, DB_BT_RECNUM) && LF_ISSET(S_STACK);
	if ((ret = __bam_lget(dbc,
	    0, pg, stack ? DB_LOCK_WRITE : DB_LOCK_READ, &lock)) != 0)
		return (ret);
	if ((ret = memp_fget(dbp->mpf, &pg, 0, &h)) != 0) {
		(void)__BT_LPUT(dbc, lock);
		return (ret);
	}

	/*
	 * Decide if we need to save this page; if we do, write lock it.
	 * We deliberately don't lock-couple on this call.  If the tree
	 * is tiny, i.e., one page, and two threads are busily updating
	 * the root page, we're almost guaranteed deadlocks galore, as
	 * each one gets a read lock and then blocks the other's attempt
	 * for a write lock.
	 */
	if (!stack &&
	    ((LF_ISSET(S_PARENT) && (u_int8_t)(stop + 1) >= h->level) ||
	    (LF_ISSET(S_WRITE) && h->level == LEAFLEVEL))) {
		(void)memp_fput(dbp->mpf, h, 0);
		(void)__BT_LPUT(dbc, lock);
		if ((ret = __bam_lget(dbc, 0, pg, DB_LOCK_WRITE, &lock)) != 0)
			return (ret);
		if ((ret = memp_fget(dbp->mpf, &pg, 0, &h)) != 0) {
			(void)__BT_LPUT(dbc, lock);
			return (ret);
		}
		stack = 1;
	}

	for (;;) {
		/*
		 * Do a binary search on the current page.  If we're searching
		 * a leaf page, we have to manipulate the indices in groups of
		 * two.  If we're searching an internal page, they're an index
		 * per page item.  If we find an exact match on a leaf page,
		 * we're done.
		 */
		jump = TYPE(h) == P_LBTREE ? P_INDX : O_INDX;
		for (base = 0,
		    lim = NUM_ENT(h) / (db_indx_t)jump; lim != 0; lim >>= 1) {
			indx = base + ((lim >> 1) * jump);
			if ((cmp =
			    __bam_cmp(dbp, key, h, indx, t->bt_compare)) == 0) {
				if (TYPE(h) == P_LBTREE)
					goto match;
				goto next;
			}
			if (cmp > 0) {
				base = indx + jump;
				--lim;
			}
		}

		/*
		 * No match found.  Base is the smallest index greater than
		 * key and may be zero or a last + O_INDX index.
		 *
		 * If it's a leaf page, return base as the "found" value.
		 * Delete only deletes exact matches.
		 */
		if (TYPE(h) == P_LBTREE) {
			*exactp = 0;

			if (LF_ISSET(S_EXACT))
				goto notfound;

			/*
			 * !!!
			 * Possibly returning a deleted record -- DB_SET_RANGE,
			 * DB_KEYFIRST and DB_KEYLAST don't require an exact
			 * match, and we don't want to walk multiple pages here
			 * to find an undeleted record.  This is handled in the
			 * __bam_c_search() routine.
			 */
			BT_STK_ENTER(cp, h, base, lock, ret);
			return (ret);
		}

		/*
		 * If it's not a leaf page, record the internal page (which is
		 * a parent page for the key).  Decrement the base by 1 if it's
		 * non-zero so that if a split later occurs, the inserted page
		 * will be to the right of the saved page.
		 */
		indx = base > 0 ? base - O_INDX : base;

		/*
		 * If we're trying to calculate the record number, sum up
		 * all the record numbers on this page up to the indx point.
		 */
		if (recnop != NULL)
			for (i = 0; i < indx; ++i)
				recno += GET_BINTERNAL(h, i)->nrecs;

next:		pg = GET_BINTERNAL(h, indx)->pgno;
		if (stack) {
			/* Return if this is the lowest page wanted. */
			if (LF_ISSET(S_PARENT) && stop == h->level) {
				BT_STK_ENTER(cp, h, indx, lock, ret);
				return (ret);
			}
			BT_STK_PUSH(cp, h, indx, lock, ret);
			if (ret != 0)
				goto err;

			if ((ret =
			    __bam_lget(dbc, 0, pg, DB_LOCK_WRITE, &lock)) != 0)
				goto err;
		} else {
			/*
			 * Decide if we want to return a reference to the next
			 * page in the return stack.  If so, lock it and never
			 * unlock it.
			 */
			if ((LF_ISSET(S_PARENT) &&
			    (u_int8_t)(stop + 1) >= (u_int8_t)(h->level - 1)) ||
			    (h->level - 1) == LEAFLEVEL)
				stack = 1;

			(void)memp_fput(dbp->mpf, h, 0);

			if ((ret =
			    __bam_lget(dbc, 1, pg, stack && LF_ISSET(S_WRITE) ?
			    DB_LOCK_WRITE : DB_LOCK_READ, &lock)) != 0)
				goto err;
		}
		if ((ret = memp_fget(dbp->mpf, &pg, 0, &h)) != 0)
			goto err;
	}
	/* NOTREACHED */

match:	*exactp = 1;

	/*
	 * If we're trying to calculate the record number, add in the
	 * offset on this page and correct for the fact that records
	 * in the tree are 0-based.
	 */
	if (recnop != NULL)
		*recnop = recno + (indx / P_INDX) + 1;

	/*
	 * If we got here, we know that we have a btree leaf page.
	 *
	 * If there are duplicates, go to the first/last one.  This is
	 * safe because we know that we're not going to leave the page,
	 * all duplicate sets that are not on overflow pages exist on a
	 * single leaf page.
	 */
	if (LF_ISSET(S_DUPLAST))
		while (indx < (db_indx_t)(NUM_ENT(h) - P_INDX) &&
		    h->inp[indx] == h->inp[indx + P_INDX])
			indx += P_INDX;
	else
		while (indx > 0 &&
		    h->inp[indx] == h->inp[indx - P_INDX])
			indx -= P_INDX;

	/*
	 * Now check if we are allowed to return deleted items; if not
	 * find the next (or previous) non-deleted item.
	 */
	if (LF_ISSET(S_DELNO)) {
		if (LF_ISSET(S_DUPLAST))
			while (B_DISSET(GET_BKEYDATA(h, indx + O_INDX)->type) &&
			    indx > 0 &&
			    h->inp[indx] == h->inp[indx - P_INDX])
				indx -= P_INDX;
		else
			while (B_DISSET(GET_BKEYDATA(h, indx + O_INDX)->type) &&
			    indx < (db_indx_t)(NUM_ENT(h) - P_INDX) &&
			    h->inp[indx] == h->inp[indx + P_INDX])
				indx += P_INDX;

		if (B_DISSET(GET_BKEYDATA(h, indx + O_INDX)->type))
			goto notfound;
	}

	BT_STK_ENTER(cp, h, indx, lock, ret);
	return (ret);

notfound:
	(void)memp_fput(dbp->mpf, h, 0);
	(void)__BT_LPUT(dbc, lock);
	ret = DB_NOTFOUND;

err:	if (cp->csp > cp->sp) {
		BT_STK_POP(cp);
		__bam_stkrel(dbc, 0);
	}
	return (ret);
}

/*
 * __bam_stkrel --
 *	Release all pages currently held in the stack.
 *
 * PUBLIC: int __bam_stkrel __P((DBC *, int));
 */
int
__bam_stkrel(dbc, nolocks)
	DBC *dbc;
	int nolocks;
{
	CURSOR *cp;
	DB *dbp;
	EPG *epg;

	dbp = dbc->dbp;
	cp = dbc->internal;

	/* Release inner pages first. */
	for (epg = cp->sp; epg <= cp->csp; ++epg) {
		if (epg->page != NULL)
			(void)memp_fput(dbp->mpf, epg->page, 0);
		if (epg->lock != LOCK_INVALID)
			if (nolocks)
				(void)__BT_LPUT(dbc, epg->lock);
			else
				(void)__BT_TLPUT(dbc, epg->lock);
	}

	/* Clear the stack, all pages have been released. */
	BT_STK_CLR(cp);

	return (0);
}

/*
 * __bam_stkgrow --
 *	Grow the stack.
 *
 * PUBLIC: int __bam_stkgrow __P((CURSOR *));
 */
int
__bam_stkgrow(cp)
	CURSOR *cp;
{
	EPG *p;
	size_t entries;
	int ret;

	entries = cp->esp - cp->sp;

	if ((ret = __os_calloc(entries * 2, sizeof(EPG), &p)) != 0)
		return (ret);
	memcpy(p, cp->sp, entries * sizeof(EPG));
	if (cp->sp != cp->stack)
		__os_free(cp->sp, entries * sizeof(EPG));
	cp->sp = p;
	cp->csp = p + entries;
	cp->esp = p + entries * 2;
	return (0);
}

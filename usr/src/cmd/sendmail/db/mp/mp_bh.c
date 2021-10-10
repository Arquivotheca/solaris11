/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 1996, 1997, 1998
 *	Sleepycat Software.  All rights reserved.
 */
#include "config.h"

#ifndef lint
static const char sccsid[] = "@(#)mp_bh.c	10.45 (Sleepycat) 11/25/98";
#endif /* not lint */

#ifndef NO_SYSTEM_INCLUDES
#include <sys/types.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#endif

#include "db_int.h"
#include "shqueue.h"
#include "db_shash.h"
#include "mp.h"
#include "common_ext.h"

static int __memp_upgrade __P((DB_MPOOL *, DB_MPOOLFILE *, MPOOLFILE *));

/*
 * __memp_bhwrite --
 *	Write the page associated with a given bucket header.
 *
 * PUBLIC: int __memp_bhwrite
 * PUBLIC:     __P((DB_MPOOL *, MPOOLFILE *, BH *, int *, int *));
 */
int
__memp_bhwrite(dbmp, mfp, bhp, restartp, wrotep)
	DB_MPOOL *dbmp;
	MPOOLFILE *mfp;
	BH *bhp;
	int *restartp, *wrotep;
{
	DB_MPOOLFILE *dbmfp;
	DB_MPREG *mpreg;
	int incremented, ret;

	if (restartp != NULL)
		*restartp = 0;
	if (wrotep != NULL)
		*wrotep = 0;
	incremented = 0;

	/*
	 * Walk the process' DB_MPOOLFILE list and find a file descriptor for
	 * the file.  We also check that the descriptor is open for writing.
	 * If we find a descriptor on the file that's not open for writing, we
	 * try and upgrade it to make it writeable.  If that fails, we're done.
	 */
	LOCKHANDLE(dbmp, dbmp->mutexp);
	for (dbmfp = TAILQ_FIRST(&dbmp->dbmfq);
	    dbmfp != NULL; dbmfp = TAILQ_NEXT(dbmfp, q))
		if (dbmfp->mfp == mfp) {
			if (F_ISSET(dbmfp, MP_READONLY) &&
			    __memp_upgrade(dbmp, dbmfp, mfp)) {
				UNLOCKHANDLE(dbmp, dbmp->mutexp);
				return (0);
			}

			/*
			 * Increment the reference count -- see the comment in
			 * memp_fclose().
			 */
			++dbmfp->ref;
			incremented = 1;
			break;
		}
	UNLOCKHANDLE(dbmp, dbmp->mutexp);
	if (dbmfp != NULL)
		goto found;

	/*
	 * It's not a page from a file we've opened.  If the file requires
	 * input/output processing, see if this process has ever registered
	 * information as to how to write this type of file.  If not, there's
	 * nothing we can do.
	 */
	if (mfp->ftype != 0) {
		LOCKHANDLE(dbmp, dbmp->mutexp);
		for (mpreg = LIST_FIRST(&dbmp->dbregq);
		    mpreg != NULL; mpreg = LIST_NEXT(mpreg, q))
			if (mpreg->ftype == mfp->ftype)
				break;
		UNLOCKHANDLE(dbmp, dbmp->mutexp);
		if (mpreg == NULL)
			return (0);
	}

	/*
	 * Try and open the file, attaching to the underlying shared area.
	 *
	 * XXX
	 * Don't try to attach to temporary files.  There are two problems in
	 * trying to do that.  First, if we have different privileges than the
	 * process that "owns" the temporary file, we might create the backing
	 * disk file such that the owning process couldn't read/write its own
	 * buffers, e.g., memp_trickle() running as root creating a file owned
	 * as root, mode 600.  Second, if the temporary file has already been
	 * created, we don't have any way of finding out what its real name is,
	 * and, even if we did, it was already unlinked (so that it won't be
	 * left if the process dies horribly).  This decision causes a problem,
	 * however: if the temporary file consumes the entire buffer cache,
	 * and the owner doesn't flush the buffers to disk, we could end up
	 * with resource starvation, and the memp_trickle() thread couldn't do
	 * anything about it.  That's a pretty unlikely scenario, though.
	 *
	 * XXX
	 * There's no negative cache, so we may repeatedly try and open files
	 * that we have previously tried (and failed) to open.
	 *
	 * Ignore any error, assume it's a permissions problem.
	 */
	if (F_ISSET(mfp, MP_TEMP))
		return (0);

	if (__memp_fopen(dbmp, mfp, R_ADDR(dbmp, mfp->path_off),
	    0, 0, mfp->stat.st_pagesize, 0, NULL, &dbmfp) != 0)
		return (0);

found:	ret = __memp_pgwrite(dbmfp, bhp, restartp, wrotep);

	if (incremented) {
		LOCKHANDLE(dbmp, dbmp->mutexp);
		--dbmfp->ref;
		UNLOCKHANDLE(dbmp, dbmp->mutexp);
	}

	return (ret);
}

/*
 * __memp_pgread --
 *	Read a page from a file.
 *
 * PUBLIC: int __memp_pgread __P((DB_MPOOLFILE *, BH *, int));
 */
int
__memp_pgread(dbmfp, bhp, can_create)
	DB_MPOOLFILE *dbmfp;
	BH *bhp;
	int can_create;
{
	DB_IO db_io;
	DB_MPOOL *dbmp;
	MPOOLFILE *mfp;
	size_t len, pagesize;
	ssize_t nr;
	int created, ret;

	dbmp = dbmfp->dbmp;
	mfp = dbmfp->mfp;
	pagesize = mfp->stat.st_pagesize;

	F_SET(bhp, BH_LOCKED | BH_TRASH);
	LOCKBUFFER(dbmp, bhp);
	UNLOCKREGION(dbmp);

	/*
	 * Temporary files may not yet have been created.  We don't create
	 * them now, we create them when the pages have to be flushed.
	 */
	nr = 0;
	if (dbmfp->fd == -1)
		ret = 0;
	else {
		/*
		 * Ignore read errors if we have permission to create the page.
		 * Assume that the page doesn't exist, and that we'll create it
		 * when we write it out.
		 */
		db_io.fd_io = dbmfp->fd;
		db_io.fd_lock = dbmp->reginfo.fd;
		db_io.mutexp =
		    F_ISSET(dbmp, MP_LOCKHANDLE) ? dbmfp->mutexp : NULL;
		db_io.pagesize = db_io.bytes = pagesize;
		db_io.pgno = bhp->pgno;
		db_io.buf = bhp->buf;

		ret = __os_io(&db_io, DB_IO_READ, &nr);
	}

	created = 0;
	if (nr < (ssize_t)pagesize)
		if (can_create)
			created = 1;
		else {
			/* If we had a short read, ret may be 0. */
			if (ret == 0)
				ret = EIO;
			__db_err(dbmp->dbenv,
			    "%s: page %lu doesn't exist, create flag not set",
			    __memp_fn(dbmfp), (u_long)bhp->pgno);
			goto err;
		}

	/*
	 * Clear any bytes we didn't read that need to be cleared.  If we're
	 * running in diagnostic mode, smash any bytes on the page that are
	 * unknown quantities for the caller.
	 */
	if (nr != (ssize_t)pagesize) {
		len = mfp->clear_len == 0 ? pagesize : mfp->clear_len;
		if (nr < (ssize_t)len)
			memset(bhp->buf + nr, 0, len - nr);
#ifdef DIAGNOSTIC
		if (nr > (ssize_t)len)
			len = nr;
		if (len < pagesize)
			memset(bhp->buf + len, 0xdb, pagesize - len);
#endif
	}

	/* Call any pgin function. */
	ret = mfp->ftype == 0 ? 0 : __memp_pg(dbmfp, bhp, 1);

	/* Unlock the buffer and reacquire the region lock. */
err:	UNLOCKBUFFER(dbmp, bhp);
	LOCKREGION(dbmp);

	/*
	 * If no errors occurred, the data is now valid, clear the BH_TRASH
	 * flag; regardless, clear the lock bit and let other threads proceed.
	 */
	F_CLR(bhp, BH_LOCKED);
	if (ret == 0) {
		F_CLR(bhp, BH_TRASH);

		/* Update the statistics. */
		if (created) {
			++dbmp->mp->stat.st_page_create;
			++mfp->stat.st_page_create;
		} else {
			++dbmp->mp->stat.st_page_in;
			++mfp->stat.st_page_in;
		}
	}

	return (ret);
}

/*
 * __memp_pgwrite --
 *	Write a page to a file.
 *
 * PUBLIC: int __memp_pgwrite __P((DB_MPOOLFILE *, BH *, int *, int *));
 */
int
__memp_pgwrite(dbmfp, bhp, restartp, wrotep)
	DB_MPOOLFILE *dbmfp;
	BH *bhp;
	int *restartp, *wrotep;
{
	DB_ENV *dbenv;
	DB_IO db_io;
	DB_LOG *lg_info;
	DB_LSN lsn;
	DB_MPOOL *dbmp;
	MPOOL *mp;
	MPOOLFILE *mfp;
	ssize_t nw;
	int callpgin, dosync, ret, syncfail;
	const char *fail;

	dbmp = dbmfp->dbmp;
	dbenv = dbmp->dbenv;
	mp = dbmp->mp;
	mfp = dbmfp->mfp;

	if (restartp != NULL)
		*restartp = 0;
	if (wrotep != NULL)
		*wrotep = 0;
	callpgin = 0;

	/*
	 * Check the dirty bit -- this buffer may have been written since we
	 * decided to write it.
	 */
	if (!F_ISSET(bhp, BH_DIRTY)) {
		if (wrotep != NULL)
			*wrotep = 1;
		return (0);
	}

	LOCKBUFFER(dbmp, bhp);

	/*
	 * If there were two writers, we may have just been waiting while the
	 * other writer completed I/O on this buffer.  Check the dirty bit one
	 * more time.
	 */
	if (!F_ISSET(bhp, BH_DIRTY)) {
		UNLOCKBUFFER(dbmp, bhp);

		if (wrotep != NULL)
			*wrotep = 1;
		return (0);
	}

	F_SET(bhp, BH_LOCKED);
	UNLOCKREGION(dbmp);

	if (restartp != NULL)
		*restartp = 1;

	/* Copy the LSN off the page if we're going to need it. */
	lg_info = dbenv->lg_info;
	if (lg_info != NULL || F_ISSET(bhp, BH_WRITE))
		memcpy(&lsn, bhp->buf + mfp->lsn_off, sizeof(DB_LSN));

	/* Ensure the appropriate log records are on disk. */
	if (lg_info != NULL && (ret = log_flush(lg_info, &lsn)) != 0)
		goto err;

	/*
	 * Call any pgout function.  We set the callpgin flag so that we flag
	 * that the contents of the buffer will need to be passed through pgin
	 * before they are reused.
	 */
	if (mfp->ftype == 0)
		ret = 0;
	else {
		callpgin = 1;
		if ((ret = __memp_pg(dbmfp, bhp, 0)) != 0)
			goto err;
	}

	/* Temporary files may not yet have been created. */
	if (dbmfp->fd == -1) {
		LOCKHANDLE(dbmp, dbmfp->mutexp);
		if (dbmfp->fd == -1 && ((ret = __db_appname(dbenv,
		    DB_APP_TMP, NULL, NULL, DB_CREATE | DB_EXCL | DB_TEMPORARY,
		    &dbmfp->fd, NULL)) != 0 || dbmfp->fd == -1)) {
			UNLOCKHANDLE(dbmp, dbmfp->mutexp);
			__db_err(dbenv,
			    "unable to create temporary backing file");
			goto err;
		}
		UNLOCKHANDLE(dbmp, dbmfp->mutexp);
	}

	/* Write the page. */
	db_io.fd_io = dbmfp->fd;
	db_io.fd_lock = dbmp->reginfo.fd;
	db_io.mutexp = F_ISSET(dbmp, MP_LOCKHANDLE) ? dbmfp->mutexp : NULL;
	db_io.pagesize = db_io.bytes = mfp->stat.st_pagesize;
	db_io.pgno = bhp->pgno;
	db_io.buf = bhp->buf;
	if ((ret = __os_io(&db_io, DB_IO_WRITE, &nw)) != 0) {
		__db_panic(dbenv, ret);
		fail = "write";
		goto syserr;
	}
	if (nw != (ssize_t)mfp->stat.st_pagesize) {
		ret = EIO;
		fail = "write";
		goto syserr;
	}

	if (wrotep != NULL)
		*wrotep = 1;

	/* Unlock the buffer and reacquire the region lock. */
	UNLOCKBUFFER(dbmp, bhp);
	LOCKREGION(dbmp);

	/*
	 * Clean up the flags based on a successful write.
	 *
	 * If we rewrote the page, it will need processing by the pgin
	 * routine before reuse.
	 */
	if (callpgin)
		F_SET(bhp, BH_CALLPGIN);
	F_CLR(bhp, BH_DIRTY | BH_LOCKED);

	/*
	 * If we write a buffer for which a checkpoint is waiting, update
	 * the count of pending buffers (both in the mpool as a whole and
	 * for this file).  If the count for this file goes to zero, set a
	 * flag so we flush the writes.
	 */
	if (F_ISSET(bhp, BH_WRITE)) {
		F_CLR(bhp, BH_WRITE);

		--mp->lsn_cnt;
		dosync = --mfp->lsn_cnt == 0 ? 1 : 0;
	} else
		dosync = 0;

	/* Update the page clean/dirty statistics. */
	++mp->stat.st_page_clean;
	--mp->stat.st_page_dirty;

	/* Update I/O statistics. */
	++mp->stat.st_page_out;
	++mfp->stat.st_page_out;

	/*
	 * Do the sync after everything else has been updated, so any incoming
	 * checkpoint doesn't see inconsistent information.
	 *
	 * XXX:
	 * Don't lock the region around the sync, fsync(2) has no atomicity
	 * issues.
	 *
	 * XXX:
	 * We ignore errors from the sync -- it makes no sense to return an
	 * error to the calling process, so set a flag causing the checkpoint
	 * to be retried later.  There is a possibility, of course, that a
	 * subsequent checkpoint was started and that we're going to force it
	 * to fail.  That should be unlikely, and fixing it would be difficult.
	 */
	if (dosync) {
		UNLOCKREGION(dbmp);
		syncfail = __os_fsync(dbmfp->fd) != 0;
		LOCKREGION(dbmp);
		if (syncfail)
			F_SET(mp, MP_LSN_RETRY);
	}

	return (0);

syserr:	__db_err(dbenv, "%s: %s failed for page %lu",
	    __memp_fn(dbmfp), fail, (u_long)bhp->pgno);

err:	/* Unlock the buffer and reacquire the region lock. */
	UNLOCKBUFFER(dbmp, bhp);
	LOCKREGION(dbmp);

	/*
	 * Clean up the flags based on a failure.
	 *
	 * The page remains dirty but we remove our lock.  If we rewrote the
	 * page, it will need processing by the pgin routine before reuse.
	 */
	if (callpgin)
		F_SET(bhp, BH_CALLPGIN);
	F_CLR(bhp, BH_LOCKED);

	return (ret);
}

/*
 * __memp_pg --
 *	Call the pgin/pgout routine.
 *
 * PUBLIC: int __memp_pg __P((DB_MPOOLFILE *, BH *, int));
 */
int
__memp_pg(dbmfp, bhp, is_pgin)
	DB_MPOOLFILE *dbmfp;
	BH *bhp;
	int is_pgin;
{
	DBT dbt, *dbtp;
	DB_MPOOL *dbmp;
	DB_MPREG *mpreg;
	MPOOLFILE *mfp;
	int ftype, ret;

	dbmp = dbmfp->dbmp;
	mfp = dbmfp->mfp;

	LOCKHANDLE(dbmp, dbmp->mutexp);

	ftype = mfp->ftype;
	for (mpreg = LIST_FIRST(&dbmp->dbregq);
	    mpreg != NULL; mpreg = LIST_NEXT(mpreg, q)) {
		if (ftype != mpreg->ftype)
			continue;
		if (mfp->pgcookie_len == 0)
			dbtp = NULL;
		else {
			dbt.size = mfp->pgcookie_len;
			dbt.data = R_ADDR(dbmp, mfp->pgcookie_off);
			dbtp = &dbt;
		}
		UNLOCKHANDLE(dbmp, dbmp->mutexp);

		if (is_pgin) {
			if (mpreg->pgin != NULL && (ret =
			    mpreg->pgin(bhp->pgno, bhp->buf, dbtp)) != 0)
				goto err;
		} else
			if (mpreg->pgout != NULL && (ret =
			    mpreg->pgout(bhp->pgno, bhp->buf, dbtp)) != 0)
				goto err;
		break;
	}

	if (mpreg == NULL)
		UNLOCKHANDLE(dbmp, dbmp->mutexp);

	return (0);

err:	UNLOCKHANDLE(dbmp, dbmp->mutexp);
	__db_err(dbmp->dbenv, "%s: %s failed for page %lu",
	    __memp_fn(dbmfp), is_pgin ? "pgin" : "pgout", (u_long)bhp->pgno);
	return (ret);
}

/*
 * __memp_bhfree --
 *	Free a bucket header and its referenced data.
 *
 * PUBLIC: void __memp_bhfree __P((DB_MPOOL *, MPOOLFILE *, BH *, int));
 */
void
__memp_bhfree(dbmp, mfp, bhp, free_mem)
	DB_MPOOL *dbmp;
	MPOOLFILE *mfp;
	BH *bhp;
	int free_mem;
{
	size_t off;

	/* Delete the buffer header from the hash bucket queue. */
	off = BUCKET(dbmp->mp, R_OFFSET(dbmp, mfp), bhp->pgno);
	SH_TAILQ_REMOVE(&dbmp->htab[off], bhp, hq, __bh);

	/* Delete the buffer header from the LRU queue. */
	SH_TAILQ_REMOVE(&dbmp->mp->bhq, bhp, q, __bh);

	/*
	 * If we're not reusing it immediately, free the buffer header
	 * and data for real.
	 */
	if (free_mem) {
		__db_shalloc_free(dbmp->addr, bhp);
		--dbmp->mp->stat.st_page_clean;
	}
}

/*
 * __memp_upgrade --
 *	Upgrade a file descriptor from readonly to readwrite.
 */
static int
__memp_upgrade(dbmp, dbmfp, mfp)
	DB_MPOOL *dbmp;
	DB_MPOOLFILE *dbmfp;
	MPOOLFILE *mfp;
{
	int fd, ret;
	char *rpath;

	/*
	 * !!!
	 * We expect the handle to already be locked.
	 */

	/* Check to see if we've already upgraded. */
	if (F_ISSET(dbmfp, MP_UPGRADE))
		return (0);

	/* Check to see if we've already failed. */
	if (F_ISSET(dbmfp, MP_UPGRADE_FAIL))
		return (1);

	/*
	 * Calculate the real name for this file and try to open it read/write.
	 * We know we have a valid pathname for the file because it's the only
	 * way we could have gotten a file descriptor of any kind.
	 */
	if ((ret = __db_appname(dbmp->dbenv, DB_APP_DATA,
	    NULL, R_ADDR(dbmp, mfp->path_off), 0, NULL, &rpath)) != 0)
		return (ret);
	if (__db_open(rpath, 0, 0, 0, &fd) != 0) {
		F_SET(dbmfp, MP_UPGRADE_FAIL);
		ret = 1;
	} else {
		/* Swap the descriptors and set the upgrade flag. */
		(void)__os_close(dbmfp->fd);
		dbmfp->fd = fd;
		F_SET(dbmfp, MP_UPGRADE);
		ret = 0;
	}
	__os_freestr(rpath);
	return (ret);
}

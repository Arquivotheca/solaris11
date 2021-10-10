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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Manage background migration of shadow filesystems.  The bulk of
 * this work is done by the rest of libshadowfs, but this file manages
 * the background thread management, as well as reporting on shadow
 * status and canceling active migration.  Each shadow handle is kept
 * in a hash indexed by dataset.  We then maintain a pool of worker
 * threads that do nothing except iterate over these handles and
 * migrate entries.  When any migration is complete, we clear the
 * migration.
 *
 * There is a single lock that must be grabbed before examining the
 * hash table state.  This prevents entries from being added or
 * removed from the table.  Each thread consumer will grab this lock,
 * iterate over the table to find the next entry, then grab the
 * per-entry rwlock for reading.  Once this lock is acquired, it can
 * drop the hash-wide lock.  Paths that need to modify this state
 * (cancel or teardown this handle) must grab both the hash lock and
 * the per-entry rwlock for writing, after which point it can do
 * whatever is needed because no other threads will have references.
 * Key to this scheme is the fact that the worker threads must not do
 * anything with the entry after dropping the read lock.  In order to
 * iterate over the table, the previous dataset name must be cached in
 * local storage and then re-looked up to continue iteration.
 *
 * Any time a file is migrated or an entry is added to the hash, a generation
 * count is updated.  If all attempts to call shadow_migrate_one() fail with
 * ESHADOW_MIGRATE_BUSY, and the generation count hasn't changed, then the
 * worker thread will go to sleep and wait for the generation count to change.
 */

#include <sys/lwp.h>
#include <sys/param.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <libshadowfs_impl.h>
#include <shadow_impl.h>

typedef struct shadow_conspiracy {
	shadow_hash_t	*sc_hash;	/* all current handles */
	pthread_mutex_t	sc_lock;	/* private hash lock */
	scf_handle_t	*sc_scf_hdl;	/* svc handle */
	scf_instance_t	*sc_inst;	/* svc instance */
	uint64_t	sc_gen;		/* generation count */
	pthread_t	*sc_workers;	/* worker pool */
	uint32_t	sc_nthread;	/* # of threads */
	pthread_cond_t	sc_cv;		/* worker thread interlock */
	uint32_t	sc_nworkers;	/* total count of worker threads */
	hrtime_t	sc_throttle;	/* throttle for fs with only errors */
	boolean_t	sc_active;	/* service online */
	boolean_t	sc_inrefresh;	/* refresh in progress */
	boolean_t	sc_shutdown;	/* shutting down service */
	boolean_t	sc_suspend;	/* workers should hold */
	int		sc_debugfd;	/* file descriptor for debug output */
	int		sc_warnfd;	/* file descriptor for warnings */
	char		*sc_fmri;	/* SMF FMRI */
} shadow_conspiracy_t;

typedef struct shadow_conspiracy_entry {
	char		*swe_dataset;	/* corresponding dataset */
	shadow_handle_t	*swe_shadow;	/* libshadowfs handle */
	char		*swe_source;	/* raw shadow source */
	shadow_hash_link_t swe_link;	/* shadow_hash link */
	pthread_rwlock_t swe_rwlock;	/* per-entry lock */
	hrtime_t	swe_next;	/* next check (for throttling) */
} shadow_conspiracy_entry_t;

static void shadcons_stop_all(shadow_conspiracy_t *scp);
static boolean_t shadcons_stop_other_workers(shadow_conspiracy_t *);

extern void _shadow_dprintf(int fd, const char *file, const char *fmt,
    va_list ap);

/*PRINTFLIKE2*/
void
shadcons_dprintf(shadow_conspiracy_t *scp, const char *fmt, ...)
{
	va_list ap;

	if ((scp)->sc_debugfd < 0)
		return;
	va_start(ap, fmt);
	_shadow_dprintf(scp->sc_debugfd, __FILE__, fmt, ap);
	va_end(ap);
}

void
shadcons_warn(shadow_conspiracy_t *scp, const char *fmt, ...)
{
	va_list ap;

	if ((scp)->sc_warnfd < 0)
		return;
	va_start(ap, fmt);
	_shadow_dprintf(scp->sc_warnfd, __FILE__, fmt, ap);
	va_end(ap);
}

static int
shadcons_translate_error(void)
{
	return (-1);
}

/*ARGSUSED*/
static void
shadcons_panic(shadow_conspiracy_t *scp)
{
	abort();
}

static void
shadcons_entry_remove(shadow_conspiracy_t *scp, shadow_conspiracy_entry_t *nsp)
{
	shadow_hash_remove(scp->sc_hash, nsp);
	shadow_close(nsp->swe_shadow);
	free(nsp->swe_source);
	free(nsp->swe_dataset);
	free(nsp);
}

static void
shadcons_mutex_enter(pthread_mutex_t *lock)
{
	int err = pthread_mutex_lock(lock);
	if (err == 0)
		return;
	if (err == EDEADLK)
		(void) fprintf(stderr, "Recursive mutex enter\n");
	else
		(void) fprintf(stderr, "Mutex lock failed: %s\n",
		    strerror(err));
	abort();
}

static void
shadcons_mutex_exit(pthread_mutex_t *lock)
{
	int err = pthread_mutex_unlock(lock);
	if (err == 0)
		return;
	(void) fprintf(stderr, "Mutex unlock failed: %s\n", strerror(err));
	ASSERT(err == 0);
}

static void *
shadow_worker(void *data)
{
	shadow_conspiracy_t *scp = data;
	char dataset[ZFS_MAXNAMELEN];
	uint64_t gen;
	shadow_conspiracy_entry_t *nsp, *start, *min;
	int ret;
	uint32_t count, size;
	pthread_t tid = pthread_self();
	hrtime_t now, mintime;
	timespec_t ts;

	shadcons_dprintf(scp, "[%d] starting\n", tid);
	dataset[0] = '\0';
	gen = count = 0;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

	shadcons_mutex_enter(&scp->sc_lock);
	for (;;) {
		if (scp->sc_shutdown) {
			shadcons_dprintf(scp, "[%d] exiting\n", tid);
			if (--scp->sc_nworkers == 0)
				(void) pthread_cond_broadcast(&scp->sc_cv);
			shadcons_mutex_exit(&scp->sc_lock);
			return (NULL);
		} else if (scp->sc_suspend) {
			shadcons_dprintf(scp, "[%d] suspending\n", tid);
			while (scp->sc_suspend)
				(void) pthread_cond_wait(&scp->sc_cv,
				    &scp->sc_lock);
		}

		size = shadow_hash_count(scp->sc_hash);

		/*
		 * Determine the next entry to check.  We try to continue where
		 * we left off, otherwise, we go back to the first entry.
		 */
		if (dataset[0] == '\0' ||
		    (nsp = shadow_hash_lookup(scp->sc_hash, dataset)) == NULL ||
		    (nsp = shadow_hash_next(scp->sc_hash, nsp)) == NULL)
			nsp = shadow_hash_first(scp->sc_hash);

		/*
		 * There was nothing in the hash.  We are either starting up or
		 * shutting down.  In either case we wait for something to
		 * change and try again.
		 */
		if (nsp == NULL) {
			shadcons_dprintf(scp,
			    "[%d] found no handles, waiting\n", tid);
			count = 0;
			(void) pthread_cond_wait(&scp->sc_cv, &scp->sc_lock);
			continue;
		}

		/*
		 * If we have filesystems with persistent errors, we want to
		 * throttle migrations so we don't spend time spinning on CPU.
		 * To do this, we set the 'swe_next' field of the shadow
		 * migration when we encounter a filesystem with only
		 * persistent errors.  We make sure we pick the filesystem with
		 * the next time interval.
		 */
		mintime = 0;
		min = NULL;
		start = nsp;
		now = gethrtime();
		while (nsp->swe_next > now) {
			if (mintime == 0 || nsp->swe_next < mintime) {
				min = nsp;
				mintime = nsp->swe_next;
			}

			nsp = shadow_hash_next(scp->sc_hash, nsp);
			if (nsp == NULL)
				nsp = shadow_hash_first(scp->sc_hash);

			if (nsp == start)
				break;
		}

		/*
		 * If the next entry is in the future, then wait for the
		 * appropriate interval.  Note that this can create a
		 * 'thundering herd' as every thread wakes up and attempts to
		 * migrate the same entry, but doing anything else adds
		 * significant complexity to the worker thread model, and this
		 * should be rare.
		 */
		if (nsp->swe_next > now) {
			ASSERT(min != NULL);
			nsp = min;
			ASSERT(nsp->swe_next > now);

			ts.tv_sec = (nsp->swe_next - now) / NANOSEC;
			ts.tv_nsec = (nsp->swe_next - now) % NANOSEC;
			shadcons_dprintf(scp,
			    "[%d] next handle not ready for %lld us, "
			    "waiting\n", tid, (nsp->swe_next - now) / 1000);
			count = 0;
			(void) pthread_cond_timedwait(&scp->sc_cv,
			    &scp->sc_lock, &ts);
			continue;
		}

		/*
		 * Check to see if we have only persistent errors.  If so,
		 * throttle the next migration.  Ideally, we'd like to check
		 * this after we've done the migration, but we need to make
		 * sure we hold the hash lock when modifying this value.
		 */
		if (shadow_migrate_only_errors(nsp->swe_shadow))
			nsp->swe_next = gethrtime() + scp->sc_throttle;
		else
			nsp->swe_next = 0;

		/*
		 * Grab the entry's reader lock before dropping the lock.
		 */
		(void) pthread_rwlock_rdlock(&nsp->swe_rwlock);
		shadcons_mutex_exit(&scp->sc_lock);

		/*
		 * At this point we can safely manipulate the entry without
		 * fear of it being deleted or modified.
		 */
		ret = shadow_migrate_one(nsp->swe_shadow);

		/*
		 * Copy the dataset name into our local storage before dropping
		 * the rwlock.  Once we do, we can't touch the shadow entry
		 * anymore.
		 */
		(void) strlcpy(dataset, nsp->swe_dataset, sizeof (dataset));
		(void) pthread_rwlock_unlock(&nsp->swe_rwlock);

		shadcons_mutex_enter(&scp->sc_lock);
		if (ret == 0) {
			/*
			 * If the migration succeeded, up the generation count
			 * (notifying consumers in the process), and record the
			 * last successfully migrated dataset.
			 */
			gen = ++scp->sc_gen;
			count = 0;
			(void) pthread_cond_broadcast(&scp->sc_cv);
		} else if (shadow_errno() == ESHADOW_MIGRATE_BUSY ||
		    shadow_errno() == ESHADOW_STANDBY) {
			/*
			 * All possible threads are busy for this particular
			 * shadow handle, or the mount is currently in standby
			 * mode.  In this case, we want to try other handles in
			 * the hash.  If we have iterated over all datasets in
			 * the hash, then we want to sleep and wait for
			 * something to change.
			 */
			if (gen == scp->sc_gen && ++count == size) {
				shadcons_dprintf(scp,
				    "[%d] migration busy, waiting\n", tid);
				(void) pthread_cond_wait(&scp->sc_cv,
				    &scp->sc_lock);
				count = 0;
			} else if (gen != scp->sc_gen) {
				shadcons_dprintf(scp,
				    "[%d] %s busy but generation "
				    "changed, continuing\n", tid, dataset);
				gen = scp->sc_gen;
				count = 0;
			} else {
				shadcons_dprintf(scp,
				    "[%d] %s busy, looking for "
				    "another handle\n", tid, dataset);
			}
		} else if (shadow_errno() == ESHADOW_MIGRATE_DONE) {
			/*
			 * Migration is complete.  First, grab the main lock
			 * and see if we're the first to notice this.
			 */
			shadcons_dprintf(scp,
			    "[%d] %s migration complete\n", tid, dataset);
			count = 0;
			if ((nsp = shadow_hash_lookup(scp->sc_hash,
			    dataset)) != NULL &&
			    shadow_migrate_finalize(nsp->swe_shadow) == 0) {
				/*
				 * If we successfully tore down the shadow
				 * migration, remove us from the hash.
				 * Ideally, we'd like to immediately dirty the
				 * cache entry (because the shadow property has
				 * been removed), but we don't have the context
				 * necessary to grab the nas class lock.  For
				 * now we just let the normal refresh catch
				 * this.
				 */
				shadcons_dprintf(scp,
				    "[%d] finalized handle\n", tid);

				shadcons_entry_remove(scp, nsp);
				gen = ++scp->sc_gen;

				if (shadcons_stop_other_workers(scp)) {
					/* above call drops lock */
					return (NULL);
				}
			}
		} else if (shadow_errno() == ESHADOW_MIGRATE_INTR) {
			shadcons_dprintf(scp,
			    "[%d] %s migration failed (%s), will try again\n",
			    tid, dataset, shadow_errmsg());
		} else {
			shadcons_dprintf(scp,
			    "[%d] %s migration failed (%s), ignoring\n",
			    tid, dataset, shadow_errmsg());
		}
	}
}

/*
 * This function is called when the first shadow handle is
 * instantiated, or the service is started when handles are active.
 * This lets us avoid creating lots of threads unless we actually need
 * them.
 */
static int
shadcons_start_workers(shadow_conspiracy_t *scp)
{
	uint32_t i, nthread;
	sigset_t oset, nset;
	pthread_t *workers;

	shadcons_mutex_enter(&scp->sc_lock);

	/*
	 * Prevent the service from starting up if we're waiting for a previous
	 * request to shutdown.
	 */
	while (scp->sc_shutdown)
		(void) pthread_cond_wait(&scp->sc_cv, &scp->sc_lock);

	scp->sc_gen++;

	if (scp->sc_active || scp->sc_workers != NULL || scp->sc_inrefresh ||
	    shadow_hash_first(scp->sc_hash) == NULL) {
		(void) pthread_cond_broadcast(&scp->sc_cv);
		shadcons_mutex_exit(&scp->sc_lock);
		return (0);
	}

	shadcons_dprintf(scp, "spawning %d worker threads\n", scp->sc_nthread);
	ASSERT(scp->sc_nthread > 0);

	if ((scp->sc_workers = shadow_zalloc(
	    scp->sc_nthread * sizeof (pthread_t))) == NULL) {
		shadcons_mutex_exit(&scp->sc_lock);
		return (-1);
	}

	(void) sigfillset(&nset);
	(void) sigdelset(&nset, SIGABRT);	/* for ASSERT() */
	(void) sigdelset(&nset, SIGCANCEL);	/* for our destroys */

	(void) pthread_sigmask(SIG_SETMASK, &nset, &oset);
	for (i = 0; i < scp->sc_nthread; i++) {
		if (pthread_create(&scp->sc_workers[i], NULL,
		    shadow_worker, scp) != 0)
			goto error;
		scp->sc_nworkers++;
	}
	(void) pthread_sigmask(SIG_SETMASK, &oset, NULL);

	scp->sc_active = B_TRUE;
	(void) pthread_cond_broadcast(&scp->sc_cv);
	shadcons_mutex_exit(&scp->sc_lock);

	return (0);

error:
	(void) pthread_sigmask(SIG_SETMASK, &oset, NULL);
	workers = scp->sc_workers;
	nthread = scp->sc_nthread;
	scp->sc_workers = NULL;

	shadcons_mutex_exit(&scp->sc_lock);

	for (i = 0; i < nthread; i++) {
		if (workers[i] == pthread_self())
			continue;
		(void) pthread_join(workers[i], NULL);
	}
	free(workers);

	return (-1);
}

static void
shadcons_stop_common(shadow_conspiracy_t *scp, boolean_t full_stop)
{
	uint32_t i, nthread;
	pthread_t *workers;

	workers = scp->sc_workers;
	nthread = scp->sc_nthread;
	scp->sc_workers = NULL;
	if (full_stop)
		scp->sc_shutdown = B_TRUE;
	else
		scp->sc_suspend = B_TRUE;

	/*
	 * We need to drop the lock while signaling other threads or else we
	 * will deadlock.  As a consequence of this, it's possible that a
	 * previous round of threads is still shutting down, so we have to look
	 * for the nworker count to go to zero when we actually want to shut
	 * down the service.
	 */
	shadcons_mutex_exit(&scp->sc_lock);

	for (i = 0; i < nthread; i++) {
		shadcons_mutex_enter(&scp->sc_lock);
		(void) pthread_cond_broadcast(&scp->sc_cv);
		shadcons_mutex_exit(&scp->sc_lock);
	}

	if (!full_stop)
		return;

	for (i = 0; i < nthread; i++) {
		int err;
		if (workers[i] == pthread_self()) {
			(void) pthread_detach(workers[i]);
			continue;
		}
		err = pthread_join(workers[i], NULL);
		if (err && err != ESRCH) {
			shadcons_panic(scp);
		}
	}
	free(workers);

	shadcons_mutex_enter(&scp->sc_lock);
	while (scp->sc_nworkers > 0)
		(void) pthread_cond_wait(&scp->sc_cv, &scp->sc_lock);
	scp->sc_shutdown = B_FALSE;
	(void) pthread_cond_broadcast(&scp->sc_cv);
	shadcons_mutex_exit(&scp->sc_lock);
}

/*
 * This function is called when the last shadow handle is torn down, or the
 * builtin service is stopped.
 */
static void
shadcons_stop_all(shadow_conspiracy_t *scp)
{
	shadcons_mutex_enter(&scp->sc_lock);

	if (scp->sc_workers == NULL) {
		scp->sc_active = B_FALSE;
		shadcons_mutex_exit(&scp->sc_lock);
		return;
	}

	shadcons_dprintf(scp, "stopping worker threads\n");
	scp->sc_active = B_FALSE;

	shadcons_stop_common(scp, B_TRUE);
}

/*
 * This is a special function that is called from a worker thread when a shadow
 * filesystem has finished migrating.  If this returns B_TRUE, then the calling
 * thread should exit as there is no more work to be done.
 */
static boolean_t
shadcons_stop_other_workers(shadow_conspiracy_t *scp)
{
	if (shadow_hash_first(scp->sc_hash) != NULL)
		return (B_FALSE);

	/*
	 * We may not be the first worker here.  Another thread may
	 * have been here and is waiting to join us below, but we got
	 * here because while she was waiting, the mount watcher
	 * thread re-added the dataset to the hash table.
	 */
	if (scp->sc_workers == NULL)
		return (B_FALSE);

	shadcons_dprintf(scp,
	    "[%d] stopping other worker threads\n", pthread_self());
	scp->sc_active = B_FALSE;

	scp->sc_nworkers--;
	shadcons_stop_common(scp, B_TRUE);
	return (B_TRUE);
}

/*
 * This function suspends any operations on the specified shadow entry.  To do
 * this, we mark the entry suspended and wait until any threads are no longer
 * operating on the entry.  This way, worker threads can continue to operate on
 * other entries entries while the operation is still going on.
 *
 * To do this, all we need to do is grab the rwlock for the entry as writer,
 * which guarantees that no migrations are in progress, and then mark the entry
 * as suspended.  However, we may have a thread in the middle of a large file
 * migration.  To allow us to still grab the lock in a reasonable amount of
 * time, we set the suspended flag with no locks held (we have the hash lock
 * held and know no one else can be setting it), poke all the threads out of
 * their current operation, and then grab the lock as writer.
 *
 * These functions must be called with the hash lock held.
 */
static void
shadcons_suspend(shadow_conspiracy_t *scp, shadow_conspiracy_entry_t *nsp)
{
	shadcons_dprintf(scp, "suspending workers\n");

	shadcons_stop_common(scp, B_FALSE);
	/* wait for # suspended == # workers? */

	(void) pthread_rwlock_wrlock(&nsp->swe_rwlock);
}

/*
 * If 'nsp' is NULL, then this is a request to resume after removing an entry.
 * In this case, we just want to check whether we have anything left in the
 * hash.  If not, then we stop all the workers.
 */
static void
shadcons_resume(shadow_conspiracy_t *scp, shadow_conspiracy_entry_t *nsp)
{
	uint32_t nthread;

	shadcons_mutex_enter(&scp->sc_lock);
	ASSERT(scp->sc_suspend);

	shadcons_dprintf(scp, "resuming workers\n");
	if (nsp != NULL)
		(void) pthread_rwlock_unlock(&nsp->swe_rwlock);

	scp->sc_suspend = B_FALSE;
	nthread = scp->sc_nthread;
	shadcons_mutex_exit(&scp->sc_lock);

	for (int i = 0; i < nthread; i++) {
		shadcons_mutex_enter(&scp->sc_lock);
		(void) pthread_cond_broadcast(&scp->sc_cv);
		shadcons_mutex_exit(&scp->sc_lock);
	}
}

static const void *
shadcons_convert(const void *arg)
{
	const shadow_conspiracy_entry_t *nsp = arg;

	return (nsp->swe_dataset);
}

static int
shadcons_scf_get_int(shadow_conspiracy_t *scp,
    const char *pgname, const char *propname, uint64_t *retval)
{
	scf_propertygroup_t *pg;
	scf_property_t *prop;
	scf_value_t *value;
	int ret = 0;

	if ((pg = scf_pg_create(scp->sc_scf_hdl)) == NULL)
		return (-1);

	if (scf_instance_get_pg_composed(scp->sc_inst,
	    NULL, pgname, pg) == -1) {
		scf_pg_destroy(pg);
		return (-1);
	}

	if ((prop = scf_property_create(scp->sc_scf_hdl)) == NULL ||
	    (value = scf_value_create(scp->sc_scf_hdl)) == NULL ||
	    scf_pg_get_property(pg, propname, prop) == -1 ||
	    scf_property_get_value(prop, value) == -1 ||
	    scf_value_get_integer(value, (int64_t *)retval) == -1)
		ret = -1;

	scf_value_destroy(value);
	scf_property_destroy(prop);
	scf_pg_destroy(pg);
	return (ret);
}

static int
shadcons_scf_get_string(shadow_conspiracy_t *scp,
    const char *pgname, const char *propname, char *buf, size_t len)
{
	scf_propertygroup_t *pg;
	scf_property_t *prop;
	scf_value_t *value;
	int ret = 0;

	if ((pg = scf_pg_create(scp->sc_scf_hdl)) == NULL)
		return (-1);


	if (scf_instance_get_pg_composed(scp->sc_inst,
	    NULL, pgname, pg) == -1) {
		scf_pg_destroy(pg);
		return (-1);
	}

	if ((prop = scf_property_create(scp->sc_scf_hdl)) == NULL ||
	    (value = scf_value_create(scp->sc_scf_hdl)) == NULL ||
	    scf_pg_get_property(pg, propname, prop) == -1 ||
	    scf_property_get_value(prop, value) == -1 ||
	    scf_value_get_astring(value, buf, len) == -1)
		ret = -1;

	scf_value_destroy(value);
	scf_property_destroy(prop);
	scf_pg_destroy(pg);
	return (ret);
}

static int
shadcons_enable_output(shadow_conspiracy_t *scp, const char *path,
    boolean_t fordebug)
{
	if (fordebug) {
		scp->sc_debugfd = open(path, O_CREAT | O_RDWR | O_APPEND,
		    0640);
		if (scp->sc_debugfd < 0)
			return (errno);
	} else {
		scp->sc_warnfd = open(path, O_RDWR | O_APPEND, 0640);
		if (scp->sc_warnfd < 0)
			return (errno);
	}
	return (0);
}

static void
shadcons_disable_output(shadow_conspiracy_t *scp, boolean_t fordebug)
{
	if (fordebug) {
		if (scp->sc_debugfd >= 0) {
			(void) close(scp->sc_debugfd);
			scp->sc_debugfd = -1;
		}
	} else {
		if (scp->sc_warnfd >= 0) {
			(void) close(scp->sc_warnfd);
			scp->sc_warnfd = -1;
		}
	}
}

static void
shadcons_set_config(shadow_conspiracy_t *scp)
{
	uint64_t threads, throttle;
	char debugfn[MAXPATHLEN];
	int err;

	shadcons_mutex_enter(&scp->sc_lock);

	/* Send warnings to stderr (they'll end up in the log) */
	err = shadcons_enable_output(scp, "/dev/stderr", B_FALSE);
	if (err) {
		(void) fprintf(stderr, "Unable to redirect warnings: %s\n",
		    strerror(err));
	}

	/* Send debug to file specified as SMF param, if any */
	if (shadcons_scf_get_string(scp, CONFIG_PARAMS, DEBUG_FILE,
	    debugfn, MAXPATHLEN) == 0) {
		err = shadcons_enable_output(scp, debugfn, B_TRUE);
		if (err)
			shadcons_warn(scp,
			    "Could not redirect debugging info to %s: %s\n",
			    debugfn, strerror(err));
		else
			shadcons_warn(scp,
			    "Debug output redirected to %s\n", debugfn);
	}

	if (shadcons_scf_get_int(scp, CONFIG_PARAMS, WORKER_THREADS,
	    &threads) != 0) {
		shadcons_warn(scp, "Could not retrieve %s/%s from SMF\n",
		    CONFIG_PARAMS, FAIL_THROTTLE);
		threads = DEFAULT_NWORKERS;
	} else if (shadcons_scf_get_int(scp, CONFIG_PARAMS, FAIL_THROTTLE,
	    &throttle) != 0) {
		shadcons_warn(scp, "Could not retrieve %s/%s from SMF\n",
		    CONFIG_PARAMS, FAIL_THROTTLE);
		throttle = DEFAULT_THROTTLE;
	}
	scp->sc_nthread = (uint32_t)threads;
	scp->sc_throttle = throttle;
	shadcons_mutex_exit(&scp->sc_lock);
}


static void
shadcons_scf_fini(shadow_conspiracy_t *scp)
{
	scf_instance_destroy(scp->sc_inst);
	(void) scf_handle_unbind(scp->sc_scf_hdl);
	scf_handle_destroy(scp->sc_scf_hdl);
}

static int
shadcons_scf_init(shadow_conspiracy_t *scp)
{
	if ((scp->sc_scf_hdl = scf_handle_create(SCF_VERSION)) == NULL ||
	    scf_handle_bind(scp->sc_scf_hdl) == -1 ||
	    (scp->sc_inst = scf_instance_create(scp->sc_scf_hdl)) == NULL ||
	    scf_handle_decode_fmri(scp->sc_scf_hdl, scp->sc_fmri, NULL, NULL,
	    scp->sc_inst, NULL, NULL, SCF_DECODE_FMRI_EXACT) == -1) {
		shadcons_scf_fini(scp);
		return (-1);
	}
	return (0);
}

static int
shadcons_cancel_common(shadow_conspiracy_t *scp, const char *dataset,
    boolean_t docancel)
{
	shadow_conspiracy_entry_t *nsp;

	shadcons_mutex_enter(&scp->sc_lock);

	if ((nsp = shadow_hash_lookup(scp->sc_hash, dataset)) == NULL) {
		shadcons_mutex_exit(&scp->sc_lock);
		return (0);
	}

	shadcons_suspend(scp, nsp);

	shadcons_mutex_enter(&scp->sc_lock);
	if (docancel && shadow_cancel(nsp->swe_shadow) != 0) {
		shadcons_mutex_exit(&scp->sc_lock);
		shadcons_resume(scp, nsp);
		return (shadcons_translate_error());
	}

	shadcons_dprintf(scp,
	    "%s shadow migration for %s\n", docancel ? "Canceling" : "Stopping",
	    nsp->swe_dataset);

	shadcons_entry_remove(scp, nsp);

	shadcons_mutex_exit(&scp->sc_lock);

	shadcons_resume(scp, NULL);

	return (0);
}

int
shadcons_cancel(shadow_conspiracy_t *scp, const char *dataset)
{
	return (shadcons_cancel_common(scp, dataset, B_TRUE));
}

void
shadcons_stop(shadow_conspiracy_t *scp, const char *dataset)
{
	(void) shadcons_cancel_common(scp, dataset, B_FALSE);
}

int
shadcons_start(shadow_conspiracy_t *scp, const char *dataset,
    const char *mountpoint, const char *source, boolean_t *is_new)
{
	shadow_conspiracy_entry_t *scep;

	shadcons_mutex_enter(&scp->sc_lock);
	if (shadow_hash_lookup(scp->sc_hash, dataset) != NULL) {
		shadcons_dprintf(scp,
		    "shadow migration in progress for %s\n", dataset);
		if (is_new)
			*is_new = B_FALSE;
		shadcons_mutex_exit(&scp->sc_lock);
		return (0);
	}

	scep = shadow_zalloc(sizeof (shadow_conspiracy_entry_t));
	if (scep == NULL) {
		shadcons_mutex_exit(&scp->sc_lock);
		return (-1);
	}

	if ((scep->swe_dataset = shadow_strdup(dataset)) == NULL) {
		free(scep);
		shadcons_mutex_exit(&scp->sc_lock);
		return (-1);
	}

	if ((scep->swe_source = shadow_strdup(source)) == NULL) {
		free(scep->swe_dataset);
		free(scep);
		shadcons_mutex_exit(&scp->sc_lock);
		return (-1);
	}

	if ((scep->swe_shadow = shadow_open(mountpoint)) == NULL) {
		free(scep->swe_dataset);
		free(scep);
		shadcons_mutex_exit(&scp->sc_lock);
		return (shadcons_translate_error());
	}

	shadcons_dprintf(scp,
	    "starting shadow migration for %s\n", scep->swe_dataset);
	shadow_hash_insert(scp->sc_hash, scep);
	shadcons_mutex_exit(&scp->sc_lock);

	if (is_new)
		*is_new = B_TRUE;

	return (shadcons_start_workers(scp));
}

int
shadcons_svc_refresh(shadow_conspiracy_t *scp)
{
	shadcons_dprintf(scp, "refreshing shadow service\n");

	shadcons_mutex_enter(&scp->sc_lock);

	/* Re-initialize SCF instance */
	shadcons_scf_fini(scp);
	if (shadcons_scf_init(scp) != 0) {
		free(scp->sc_fmri);
		scp->sc_fmri = NULL;
		shadcons_mutex_exit(&scp->sc_lock);
		return (-1);
	}

	/*
	 * We have to set 'inrefresh' here so that someone else doesn't come
	 * along and start the workers before we have a chance to change the
	 * thread count.
	 */
	scp->sc_inrefresh = B_TRUE;
	shadcons_mutex_exit(&scp->sc_lock);

	shadcons_stop_all(scp);
	shadcons_set_config(scp);

	shadcons_mutex_enter(&scp->sc_lock);
	scp->sc_inrefresh = B_FALSE;
	shadcons_mutex_exit(&scp->sc_lock);

	return (shadcons_start_workers(scp));
}

/*ARGSUSED*/
int
shadcons_svc_start(shadow_conspiracy_t *scp)
{
	shadcons_dprintf(scp, "starting shadow service\n");
	return (shadcons_start_workers(scp));
}

/*ARGSUSED*/
int
shadcons_svc_stop(shadow_conspiracy_t *scp)
{
	shadcons_dprintf(scp, "stopping shadow service\n");
	shadcons_stop_all(scp);
	return (0);
}

int
shadcons_status(shadow_conspiracy_t *scp, const char *dataset,
    shadow_conspiracy_status_t *statp)
{
	shadow_conspiracy_entry_t *nsp;
	shadow_status_t stat;
	hrtime_t now;

	shadcons_mutex_enter(&scp->sc_lock);
	if ((nsp = shadow_hash_lookup(scp->sc_hash, dataset)) == NULL) {
		shadcons_mutex_exit(&scp->sc_lock);
		return (ENOENT);
	}

	shadow_get_status(nsp->swe_shadow, &stat);
	statp->scs_complete = shadow_migrate_only_errors(nsp->swe_shadow);
	shadcons_mutex_exit(&scp->sc_lock);

	if ((statp->scs_ds_name = shadow_strdup(dataset)) == NULL)
		return (ENOMEM);

	now = gethrtime();

	if ((now - stat.ss_start) / NANOSEC < 60 ||
	    stat.ss_processed == 0 ||
	    stat.ss_estimated == 0) {
		statp->scs_remaining = -1ULL;
	} else {
		statp->scs_remaining = stat.ss_estimated;
	}

	statp->scs_xferred = stat.ss_processed;
	statp->scs_errors = stat.ss_errors;
	statp->scs_elapsed = (now - stat.ss_start) / NANOSEC;

	return (0);
}

/*
 * Allocates an array of conspiracy stats for each dataset
 * currently being migrated.  Caller must free the array.
 */
int
shadcons_status_all(shadow_conspiracy_t *scp,
    shadow_conspiracy_status_t **statp, size_t *nstats)
{
	shadow_conspiracy_status_t *sca;
	shadow_conspiracy_status_t *scsp;
	shadow_conspiracy_entry_t *nsp;
	shadow_status_t stat;
	size_t nelems;
	hrtime_t now;
	int elem;

	shadcons_mutex_enter(&scp->sc_lock);

	nelems = shadow_hash_count(scp->sc_hash);
	if (nelems == 0) {
		*nstats = 0;
		shadcons_mutex_exit(&scp->sc_lock);
		return (0);
	}

	sca = shadow_zalloc(nelems * sizeof (shadow_conspiracy_status_t));
	if (sca == NULL) {
		*nstats = 0;
		shadcons_mutex_exit(&scp->sc_lock);
		return (-1);
	}

	elem = 0;
	nsp = shadow_hash_first(scp->sc_hash);
	while (nsp != NULL) {
		shadow_get_status(nsp->swe_shadow, &stat);
		scsp = &sca[elem++];

		scsp->scs_ds_name =
		    shadow_strdup(scp->sc_hash->sh_convert(nsp));
		if (scsp->scs_ds_name == NULL) {
			for (int e = 0; e < elem - 1; e++)
				free(sca[e].scs_ds_name);
			free(sca);
			*nstats = 0;
			shadcons_mutex_exit(&scp->sc_lock);
			return (-1);
		}
		now = gethrtime();

		if ((now - stat.ss_start) / NANOSEC < 60 ||
		    stat.ss_processed == 0 || stat.ss_estimated == 0) {
			scsp->scs_remaining = -1ULL;
		} else {
			scsp->scs_remaining = stat.ss_estimated;
		}
		scsp->scs_xferred = stat.ss_processed;
		scsp->scs_errors = stat.ss_errors;
		scsp->scs_elapsed = (now - stat.ss_start) / NANOSEC;
		scsp->scs_complete =
		    shadow_migrate_only_errors(nsp->swe_shadow);
		nsp = shadow_hash_next(scp->sc_hash, nsp);
	}
	ASSERT(elem == nelems);
	shadcons_mutex_exit(&scp->sc_lock);

	*statp = sca;
	*nstats = nelems;
	return (0);
}

size_t
shadcons_hash_count(shadow_conspiracy_t *scp)
{
	return (shadow_hash_count(scp->sc_hash));
}

void *
shadcons_hash_first(shadow_conspiracy_t *scp)
{
	return (shadow_hash_first(scp->sc_hash));
}

void *
shadcons_hash_next(shadow_conspiracy_t *scp, void *elem)
{
	return (shadow_hash_next(scp->sc_hash, elem));
}

void
shadcons_hash_remove(shadow_conspiracy_t *scp, void *elem)
{
	shadow_hash_remove(scp->sc_hash, elem);
}

const char *
shadcons_hashentry_dataset(shadow_conspiracy_t *scp, void *entry)
{
	return (scp->sc_hash->sh_convert(entry));
}

void
shadcons_lock(shadow_conspiracy_t *scp)
{
	shadcons_mutex_enter(&scp->sc_lock);
}

void
shadcons_unlock(shadow_conspiracy_t *scp)
{
	shadcons_mutex_exit(&scp->sc_lock);
}

/*ARGSUSED*/
void
shadcons_fini(shadow_conspiracy_t *scp)
{
	if (scp == NULL)
		return;

	shadcons_disable_output(scp, B_TRUE);
	shadcons_disable_output(scp, B_FALSE);
	shadcons_scf_fini(scp);
	shadow_hash_destroy(scp->sc_hash);
	(void) pthread_cond_destroy(&scp->sc_cv);
	(void) pthread_mutex_destroy(&scp->sc_lock);
	free(scp->sc_fmri);
	free(scp);
}

shadow_conspiracy_t *
shadcons_init(const char *fmri)
{
	pthread_mutexattr_t mtype;
	shadow_conspiracy_t *scp;

	if ((scp = shadow_zalloc(sizeof (shadow_conspiracy_t))) == NULL)
		return (NULL);

	(void) pthread_mutexattr_init(&mtype);
	(void) pthread_mutexattr_settype(&mtype, PTHREAD_MUTEX_ERRORCHECK);
	if (pthread_mutex_init(&scp->sc_lock, &mtype) != 0) {
		free(scp);
		return (NULL);
	}
	if (pthread_cond_init(&scp->sc_cv, NULL) != 0) {
		(void) pthread_mutex_destroy(&scp->sc_lock);
		free(scp);
		return (NULL);
	}

	scp->sc_debugfd = scp->sc_warnfd = -1;

	if ((scp->sc_fmri = shadow_strdup(fmri)) == NULL) {
		(void) pthread_cond_destroy(&scp->sc_cv);
		(void) pthread_mutex_destroy(&scp->sc_lock);
		free(scp);
		return (NULL);
	}

	if (shadcons_scf_init(scp) != 0) {
		(void) pthread_cond_destroy(&scp->sc_cv);
		(void) pthread_mutex_destroy(&scp->sc_lock);
		free(scp->sc_fmri);
		free(scp);
		return (NULL);
	}

	shadcons_set_config(scp);

	if ((scp->sc_hash = shadow_hash_create(
	    offsetof(shadow_conspiracy_entry_t, swe_link),
	    shadcons_convert, shadow_hash_strhash,
	    shadow_hash_strcmp)) == NULL) {
		(void) pthread_cond_destroy(&scp->sc_cv);
		(void) pthread_mutex_destroy(&scp->sc_lock);
		free(scp->sc_fmri);
		free(scp);
		return (NULL);
	}

	return (scp);
}

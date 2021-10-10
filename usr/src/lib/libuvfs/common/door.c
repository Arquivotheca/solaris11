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


#include <stdio.h>
#include <door.h>
#include <sys/vfs.h>
#include <assert.h>
#include <sys/errno.h>
#include <unistd.h>
#include <strings.h>
#include <pthread.h>
#include <thread.h>
#include <synch.h>
#include <libuvfs_impl.h>
#include <errno.h>
#include <atomic.h>

static pthread_key_t libuvfs_tsd_ucred_key = PTHREAD_ONCE_KEY_NP;

static ucred_t *
libuvfs_callback_get_ucred(void)
{
	ucred_t *ucred, *orig;

	orig = ucred = pthread_getspecific(libuvfs_tsd_ucred_key);

	(void) door_ucred(&ucred);

	if (orig == NULL)
		(void) pthread_setspecific(libuvfs_tsd_ucred_key, ucred);

	return (ucred);
}

static void
libuvfs_callback_door_unref(libuvfs_fs_t *fs)
{
	(void) mutex_lock(&fs->fs_lock);
	fs->fs_flags &= ~LIBUVFS_FS_FLAG_DOOR_VALID;
	(void) cond_broadcast(&fs->fs_daemon_cv);
	(void) mutex_unlock(&fs->fs_lock);

	(void) atexit(libuvfs_daemon_atexit);
}

/*ARGSUSED*/
static void
libuvfs_callback_server(void *vfs, char *arg, size_t argsize,
    door_desc_t *desc, uint_t ndesc)
{
	libuvfs_fs_t *fs = vfs;
	libuvfs_common_arg_t *carg = (libuvfs_common_arg_t *)(uintptr_t)arg;
	libuvfs_callback_reg_t *reg;
	libuvfs_callback_t callback;
	libuvfs_common_res_t res;
	ucred_t *ucred;

	/*
	 * If our arg is DOOR_UNREF_DATA, then the kernel has dropped
	 * all references to our door, which means that we should exit.
	 */
	if (arg == DOOR_UNREF_DATA) {
		libuvfs_callback_door_unref(fs);
		return;
	}

	if ((argsize < sizeof (libuvfs_common_arg_t)) ||
	    (carg->lca_optag >= UVFS_CB_NUM_OPS)) {
		res.lcr_error = EINVAL;
		libuvfs_return(&res, sizeof (res));
	}

	/*
	 * No need to take locks on fs, since callbacks may not change after
	 * the door is created.
	 */
	reg = &fs->fs_callback[carg->lca_optag];
	callback = reg->lcr_callback;

	if (callback == NULL) {
		res.lcr_error = ENOSYS;
		libuvfs_return(&res, sizeof (res));
	}

	ucred = libuvfs_callback_get_ucred();
	if (ucred == NULL) {
		res.lcr_error = EIO;
		libuvfs_return(&res, sizeof (res));
	}

	callback(fs, arg, argsize, ucred);

	/*
	 * We should never get here if the callback correctly called
	 * libuvfs_return() (which contains the door_return() call).
	 */
	abort();
}

int
libuvfs_register_callback(libuvfs_fs_t *fs,
    const libuvfs_callback_reg_t *reg)
{
	(void) mutex_lock(&fs->fs_lock);

	if (fs->fs_flags & LIBUVFS_FS_FLAG_DOOR_VALID) {
		(void) mutex_unlock(&fs->fs_lock);
		return (-1);
	}

	fs->fs_callback[reg->lcr_optag] = *reg; /* struct assign */

	(void) mutex_unlock(&fs->fs_lock);

	return (0);
}

int
libuvfs_register_callbacks(libuvfs_fs_t *fs,
    const libuvfs_callback_reg_t *callbacks)
{
	const libuvfs_callback_reg_t *reg;

	(void) mutex_lock(&fs->fs_lock);

	if (fs->fs_flags & LIBUVFS_FS_FLAG_DOOR_VALID) {
		(void) mutex_unlock(&fs->fs_lock);
		return (-1);
	}

	for (reg = callbacks; reg->lcr_callback; reg++)
		fs->fs_callback[reg->lcr_optag] = *reg; /* struct assign */

	(void) mutex_unlock(&fs->fs_lock);

	return (0);
}

static void
libuvfs_ucred_free(void *vucred)
{
	ucred_t *ucred = vucred;

	ucred_free(ucred);
}

/*ARGSUSED*/
static void
libuvfs_worker_thread_prep(void *foo)
{
	(void) pthread_key_create_once_np(&libuvfs_tsd_ucred_key,
	    libuvfs_ucred_free);
	(void) pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
}

/*
 * Create a new worker thread, unless we're at the limit.
 */

/*ARGSUSED*/
static int
libuvfs_worker_create(door_info_t *dinfo, void *(*tfunc)(void *), void *targ,
    void *vfs)
{
	libuvfs_fs_t *fs = vfs;
	uint32_t current;

	current = atomic_add_32_nv(&fs->fs_cur_dthreads, 1);
	if (current > fs->fs_max_dthreads) {
		atomic_add_32(&fs->fs_cur_dthreads, -1);
		return (0);
	}

	if (pthread_create(NULL, &fs->fs_pthread_attr, tfunc, targ) != 0)
		return (-1);

	return (1);
}

int
libuvfs_daemon_ready(libuvfs_fs_t *fs)
{
	int rc;

	(void) mutex_lock(&fs->fs_lock);

	rc = libuvfs_set_fsparam(fs);
	if (rc != 0)
		goto errout;

	rc = door_xcreate(libuvfs_callback_server, fs,
	    DOOR_NO_CANCEL | DOOR_UNREF | DOOR_REFUSE_DESC,
	    libuvfs_worker_create, libuvfs_worker_thread_prep, fs, 1);
	if (rc == -1)
		goto errout;
	fs->fs_door = rc;
	fs->fs_flags |= LIBUVFS_FS_FLAG_DOOR_VALID;

	(void) mutex_unlock(&fs->fs_lock);

	rc = libuvfs_daemon_register(fs);
	if (rc != 0)
		goto errout;

	/*
	 * Here we wait.  Other threads, the threads servicing the door
	 * upcalls, will do all of the work.
	 */
	(void) mutex_lock(&fs->fs_lock);
	while (fs->fs_flags & LIBUVFS_FS_FLAG_DOOR_VALID)
		(void) cond_wait(&fs->fs_daemon_cv, &fs->fs_lock);
	(void) mutex_unlock(&fs->fs_lock);

	return (0);

errout:
	if (libuvfs_is_daemon())
		libuvfs_daemon_exit(fs);

	return (rc);
}

/*ARGSUSED*/
void
libuvfs_return(void *res, size_t res_size)
{
	int error;
	libuvfs_common_res_t *rres = res;

	errno = 0;
	error = door_return((char *)res, res_size, NULL, 0);

	if (error == -1) {
		rres->lcr_error = errno;
		(void) door_return((char *)rres, sizeof (libuvfs_common_res_t),
		    NULL, 0);
	}
}

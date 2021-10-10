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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Migration status
 *
 * Tracking the progress of a shadow migration is tricky business.  On the
 * surface, it would seem as simple as looking at statvfs(2) output and then
 * subtract values from stat(2) as we process each file.  Sadly, this only
 * works when we are migrating a single filesystem and are starting at the
 * root.  Because we have to handle migrating sub-directories as well as nested
 * filesystems, and there is no way to detect if this is the case over NFS, we
 * have to resort to heuristics to guess at the amount of pending data.  This
 * is also complicated by the fact that, depending on the filesystem, the
 * number of blocks in the filesystem may include all metadata, while
 * individual files may not.
 *
 * We can always keep track of the total size of plain file contents that we
 * have migrated, but it's much more difficult to estimate how much is
 * remaining.  There's no good answer here, but we can take a wild swing and
 * come up with something that's hopefully useful.
 *
 * 	k	Number of directory entries migrated
 * 	b	Average number of subdirectories in interior directories
 * 	h	Average depth of all leaf directories
 * 	r	Number of directory entries in the queue
 * 	x	Average depth of directory entries in the queue
 *
 * These can be then fed into the following equation to produce a completion
 * percentage:
 *
 *			    k
 *		-------------------------
 *		k + ln(r) * (b ^ (h - x))
 *
 * The intent of this equation is to keep track of things with simple global
 * constants, but to make sure that it approaches 1 as 'k' gets large.
 *
 * One possible improvement is to factor the standard deviation into the
 * calculation for 'h' so that we assume some pessimistic deeper depth.
 */

#include "shadow_impl.h"

/*
 * Called when processing is done for a directory or file.  For directories,
 * 'size' is the number of sub-directories.  For files, it's the total size of
 * the file (minus holes).
 */
void
shadow_status_update(shadow_handle_t *shp, shadow_entry_t *sep,
    uint64_t size, uint64_t subdirs)
{
	if (sep != NULL && sep->se_type == SHADOW_TYPE_DIR) {
		if (subdirs == 0) {
			shp->sh_progress.sp_leaf++;
			shp->sh_progress.sp_leaf_depth += sep->se_depth;
		} else {
			shp->sh_progress.sp_interior++;
		}
	}

	shp->sh_progress.sp_processed += size;
}

/*
 * Called when an entry is added to the queue.  If this is a directory, keep
 * track of the in-queue statistics.
 */
void
shadow_status_enqueue(shadow_handle_t *shp, shadow_entry_t *sep)
{
	if (sep->se_type != SHADOW_TYPE_DIR)
		return;

	shp->sh_progress.sp_dir_seen++;
	shp->sh_progress.sp_dir_queue++;
	shp->sh_progress.sp_dir_depth += sep->se_depth;
}

/*
 * Called when an entry is removed from the queue.  Only meaningful for
 * directories.
 */
void
shadow_status_dequeue(shadow_handle_t *shp, shadow_entry_t *sep)
{
	if (sep->se_type != SHADOW_TYPE_DIR)
		return;

	shp->sh_progress.sp_dir_queue--;
	shp->sh_progress.sp_dir_depth -= sep->se_depth;
}

/*
 * The only user-visible status function.  This does the above calculation, and
 * then presents the user with a simplified interface (processed and estimated
 * remaining).
 */
void
shadow_get_status(shadow_handle_t *shp, shadow_status_t *ssp)
{
	double k, b, h, r, x, percent;
	shadow_progress_t *p = &shp->sh_progress;

	(void) pthread_mutex_lock(&shp->sh_lock);
	k = (double)p->sp_dir_seen;
	b = (double)p->sp_dir_seen / p->sp_interior;
	h = (double)p->sp_leaf_depth / p->sp_leaf;
	r = (double)p->sp_dir_queue;
	x = (double)p->sp_dir_depth / p->sp_dir_queue;
	(void) pthread_mutex_unlock(&shp->sh_lock);

#if 0
	(void) printf("dir_seen = %lld\n", p->sp_dir_seen);
	(void) printf("interior = %lld\n", p->sp_interior);
	(void) printf("leaf = %lld\n", p->sp_leaf);
	(void) printf("leaf_depth = %lld\n", p->sp_leaf_depth);
	(void) printf("dir_queue = %lld\n", p->sp_dir_queue);
	(void) printf("dir_depth = %lld\n", p->sp_dir_depth);
	(void) printf("\n");

	(void) printf("k = %f\n", k);
	(void) printf("b = %f\n", b);
	(void) printf("h = %f\n", h);
	(void) printf("r = %f\n", r);
	(void) printf("x = %f\n", x);
#endif

	percent = k / (k + (log(r + 1) * pow(b, h - x + 2)));

	ssp->ss_processed = p->sp_processed;
	ssp->ss_start = shp->sh_start;
	if (p->sp_dir_seen == 0 || ssp->ss_processed == 0)
		ssp->ss_estimated = 0;
	else
		ssp->ss_estimated = (uint64_t)
		    (p->sp_processed / percent - p->sp_processed);

	(void) pthread_mutex_lock(&shp->sh_errlock);
	ssp->ss_errors = shp->sh_errcount;
	(void) pthread_mutex_unlock(&shp->sh_errlock);

}

/*
 * This function returns details about the current set of errors.  It assumes
 * that the user has gotten the number of errors from shadow_get_status().
 * Because the total number of errors doesn't decrease, we don't have to worry
 * if there isn't enough to fill.  If the number of errors has subsequently
 * risen, we only return what the user has asked for.
 */
shadow_error_report_t *
shadow_get_errors(shadow_handle_t *shp, size_t count)
{
	shadow_error_report_t *errors;
	shadow_error_t *sep;
	size_t i;

	assert(count > 0);

	if ((errors = shadow_zalloc(count *
	    sizeof (shadow_error_report_t))) == NULL)
		return (NULL);

	(void) pthread_mutex_lock(&shp->sh_errlock);
	assert(count >= shp->sh_errcount);
	for (i = 0, sep = shp->sh_errors; i < count; sep = sep->se_next, i++) {
		errors[i].ser_errno = sep->se_error;
		if ((errors[i].ser_path =
		    shadow_strdup(sep->se_path)) == NULL) {
			(void) pthread_mutex_unlock(&shp->sh_errlock);
			shadow_free_errors(errors, count);
			return (NULL);
		}
	}
	(void) pthread_mutex_unlock(&shp->sh_errlock);

	return (errors);
}

void
shadow_free_errors(shadow_error_report_t *errors, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		free(errors[i].ser_path);
	free(errors);
}

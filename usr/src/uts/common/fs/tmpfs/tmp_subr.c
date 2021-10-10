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
 * Copyright (c) 1990, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/debug.h>
#include <sys/time.h>
#include <sys/cmn_err.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/cred.h>
#include <sys/kmem.h>
#include <sys/atomic.h>
#include <sys/policy.h>
#include <sys/fs/tmp.h>
#include <sys/fs/tmpnode.h>

#define	MODESHIFT	3

int
tmp_taccess(void *vtp, int mode, struct cred *cred)
{
	struct tmpnode *tp = vtp;
	int shift = 0;
	/*
	 * Check access based on owner, group and
	 * public permissions in tmpnode.
	 */
	if (crgetuid(cred) != tp->tn_uid) {
		shift += MODESHIFT;
		if (groupmember(tp->tn_gid, cred) == 0)
			shift += MODESHIFT;
	}

	return (secpolicy_vnode_access2(cred, TNTOV(tp), tp->tn_uid,
	    tp->tn_mode << shift, mode));
}

/*
 * Decide whether it is okay to remove within a sticky directory.
 * Two conditions need to be met:  write access to the directory
 * is needed.  In sticky directories, write access is not sufficient;
 * you can remove entries from a directory only if you own the directory,
 * if you are privileged, if you own the entry or if they entry is
 * a plain file and you have write access to that file.
 * Function returns 0 if remove access is granted.
 */

int
tmp_sticky_remove_access(struct tmpnode *dir, struct tmpnode *entry,
	struct cred *cr)
{
	uid_t uid = crgetuid(cr);

	if ((dir->tn_mode & S_ISVTX) &&
	    uid != dir->tn_uid &&
	    uid != entry->tn_uid &&
	    (entry->tn_type != VREG ||
	    tmp_taccess(entry, VWRITE, cr) != 0))
		return (secpolicy_vnode_remove(cr));

	return (0);
}

/*
 * Allocate zeroed memory if tmpfs_maxkmem has not been exceeded
 * or the 'musthave' flag is set.  'musthave' allocations should
 * always be subordinate to normal allocations so that tmpfs_maxkmem
 * can't be exceeded by more than a few KB.  Example: when creating
 * a new directory, the tmpnode is a normal allocation; if that
 * succeeds, the dirents for "." and ".." are 'musthave' allocations.
 */
void *
tmp_memalloc(size_t size, int musthave)
{
	static time_t last_warning;
	time_t now;

	if (atomic_add_long_nv(&tmp_kmemspace, size) < tmpfs_maxkmem ||
	    musthave)
		return (kmem_zalloc(size, KM_SLEEP));

	atomic_add_long(&tmp_kmemspace, -size);
	now = gethrestime_sec();
	if (last_warning != now) {
		last_warning = now;
		cmn_err(CE_WARN, "tmp_memalloc: tmpfs over memory limit");
	}
	return (NULL);
}

void
tmp_memfree(void *cp, size_t size)
{
	kmem_free(cp, size);
	atomic_add_long(&tmp_kmemspace, -size);
}

/*
 * Convert a string containing a number (number of bytes) to a pgcnt_t,
 * containing the corresponding number of pages. On 32-bit kernels, the
 * maximum value encoded in 'str' is PAGESIZE * ULONG_MAX, while the value
 * returned in 'maxpg' is at most ULONG_MAX.
 *
 * If the number is followed by a "k" or "K", the value is converted from
 * kilobytes to bytes.  If it is followed by an "m" or "M" it is converted
 * from megabytes to bytes.  If it is not followed by a character it is
 * assumed to be in bytes. Multiple letter options are allowed, so for instance
 * '2mk' is interpreted as 2gb.
 *
 * Parse and overflow errors are detected and a non-zero number returned on
 * error.
 */

int
tmp_convnum(char *str, pgcnt_t *maxpg)
{
	uint64_t num = 0, oldnum;
#ifdef _LP64
	uint64_t max_bytes = ULONG_MAX;
#else
	uint64_t max_bytes = PAGESIZE * (uint64_t)ULONG_MAX;
#endif
	char *c;

	if (str == NULL)
		return (EINVAL);
	c = str;

	/*
	 * Convert str to number
	 */
	while ((*c >= '0') && (*c <= '9')) {
		oldnum = num;
		num = num * 10 + (*c++ - '0');
		if (oldnum > num) /* overflow */
			return (EINVAL);
	}

	/*
	 * Terminate on null
	 */
	while (*c != '\0') {
		switch (*c++) {

		/*
		 * convert from kilobytes
		 */
		case 'k':
		case 'K':
			if (num > max_bytes / 1024) /* will overflow */
				return (EINVAL);
			num *= 1024;
			break;

		/*
		 * convert from megabytes
		 */
		case 'm':
		case 'M':
			if (num > max_bytes / (1024 * 1024)) /* will overflow */
				return (EINVAL);
			num *= 1024 * 1024;
			break;

		default:
			return (EINVAL);
		}
	}

	/*
	 * Since btopr() rounds up to page granularity, this round-up can
	 * cause an overflow only if 'num' is between (max_bytes - PAGESIZE)
	 * and (max_bytes). In this case the resulting number is zero, which
	 * is what we check for below.
	 */
	if ((*maxpg = (pgcnt_t)btopr(num)) == 0 && num != 0)
		return (EINVAL);
	return (0);
}

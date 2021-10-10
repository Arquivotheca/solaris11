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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_FS_ZFS_STAT_H
#define	_SYS_FS_ZFS_STAT_H

#ifdef _KERNEL
#include <sys/isa_defs.h>
#include <sys/types32.h>
#include <sys/dmu.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif
/*
 * A limited number of zpl level stats are retrievable
 * with an ioctl.  zfs diff is the current consumer.
 * These are passed to the zfs diff callback.
 */
typedef struct zfs_stat {
	uint64_t	zs_obj;		/* object number */
	uint64_t	zs_nameoff;	/* offset from start of zfs_bulk_stat */
	uint64_t	zs_nameerr;	/* 0 if name is present */
	uint64_t	zs_gen;
	uint64_t	zs_mode;
	uint64_t	zs_links;
	uint64_t	zs_ctime[2];
	uint64_t	zs_atime[2];
	uint64_t	zs_mtime[2];
	uint64_t	zs_crtime[2];
	uint64_t	zs_parent;
	uint64_t	zs_size;
	uint64_t	zs_uid;
	uint64_t	zs_gid;
} zfs_stat_t;

/* For zfs_stat_t->nameerr field */
#define	ZFS_NAMEERR_NO_NAME_REQUESTED	-1ULL

#define	MAX_ZFS_BULK_STATS_BUF_LEN	(5 * 1024 * 1024)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_ZFS_STAT_H */

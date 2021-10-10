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
#include <strings.h>
#include <fcntl.h>
#include <sys/types.h>
#include <attr.h>
#include <libnvpair.h>
#include <unistd.h>
#include <libuvfs_impl.h>
#include <pthread.h>
#include <sys/fs/uvfs.h>
#include <sys/sysmacros.h>

static int
open_device(libuvfs_fs_t *fs)
{
	int f;

	f = open(UVFS_DEV, O_RDONLY);
	if (f < 0)
		return (-1);

	fs->fs_dev = f;

	return (0);
}

int
libuvfs_daemon_start_wait(libuvfs_fs_t *fs, uint32_t wait_usec)
{
	uvfs_ioc_daemon_wait_t args;
	int rc;

	if (fs->fs_dev < 0) {
		rc = open_device(fs);
		if (rc != 0)
			return (rc);
	}

	args.uidw_fsid = fs->fs_fsid;
	args.uidw_wait_usec = wait_usec;
	rc = ioctl(fs->fs_dev, UVFS_IOC_DAEMON_WAIT, &args);

	return (rc);
}

int
libuvfs_set_fsparam(libuvfs_fs_t *fs)
{
	uvfs_ioc_fsparam_get_t args;
	int rc;

	if (fs->fs_dev < 0) {
		rc = open_device(fs);
		if (rc != 0)
			return (rc);
	}

	args.upar_fsid = fs->fs_fsid;
	rc = ioctl(fs->fs_dev, UVFS_IOC_FSPARAM_GET, &args);
	if (rc != 0)
		return (rc);

	fs->fs_io_maxread = args.upar_maxread;
	fs->fs_io_maxwrite = args.upar_maxwrite;
	fs->fs_max_dthreads = args.upar_max_dthreads;

	(void) pthread_attr_setstacksize(&fs->fs_pthread_attr,
	    MAX(args.upar_maxwrite, args.upar_maxread) +
	    LIBUVFS_STACK_OVERHEAD);

	return (0);
}

int
libuvfs_daemon_register(libuvfs_fs_t *fs)
{
	uvfs_ioc_daemon_register_t args;
	int rc;

	args.uidr_fsid = fs->fs_fsid;
	args.uidr_door = fs->fs_door;
	rc = ioctl(fs->fs_dev, UVFS_IOC_DAEMON_REGISTER, &args);

	if (rc != 0)
		(void) atexit(libuvfs_daemon_atexit);

	return (rc);
}

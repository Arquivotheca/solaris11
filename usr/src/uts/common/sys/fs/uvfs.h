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

#ifndef _UVFS_H
#define	_UVFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#define	UVFS_DRIVER	"uvfs"
#define	UVFS_DEV	"/dev/uvfs"

#define	UVFS_VERSION_STRING		"1"

/*
 * /dev/uvfs ioctl numbers.
 */
#define	UVFS_IOC		('V' << 8)

typedef enum {
	UVFS_IOC_DAEMON_WAIT = UVFS_IOC,
	UVFS_IOC_FSPARAM_GET,
	UVFS_IOC_DAEMON_REGISTER
} uvfs_ioc_t;

typedef struct {
	uint64_t uidw_fsid;
	uint64_t uidw_wait_usec;
} uvfs_ioc_daemon_wait_t;

typedef struct {
	uint64_t upar_fsid;
	uint64_t upar_maxread;
	uint64_t upar_maxwrite;
	uint64_t upar_max_dthreads;
} uvfs_ioc_fsparam_get_t;


typedef struct {
	uint64_t uidr_fsid;
	uint64_t uidr_door;
} uvfs_ioc_daemon_register_t;


/* Request flags */
#ifdef __cplusplus
}
#endif

#endif /* _UVFS_H */

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
 * Copyright (c) 1988, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

#pragma weak _shmat = shmat
#pragma weak _shmctl = shmctl
#pragma weak _shmctl64 = shmctl64
#pragma weak _shmdt = shmdt
#pragma weak _shmget = shmget
#pragma weak _shmids = shmids

#include "lint.h"
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/ipc_impl.h>
#include <sys/shm.h>
#include <sys/shm_impl.h>
#include <sys/syscall.h>
#include <errno.h>

void *
shmat(int shmid, const void *shmaddr, int shmflg)
{
	sysret_t rval;
	int error;

	error = __systemcall(&rval, SYS_shmsys, SHMAT, shmid, shmaddr, shmflg);
	if (error)
		(void) __set_errno(error);
	return ((void *)rval.sys_rval1);
}

int
shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
	if (cmd == IPC_SET64 || cmd == IPC_STAT64 || cmd == IPC_XSTAT64) {
		(void) __set_errno(EINVAL);
		return (-1);
	}
	return (syscall(SYS_shmsys, SHMCTL, shmid, cmd, buf));
}

int
shmctl64(int shmid, int cmd, struct shmid_ds64 *buf)
{
	if (cmd != IPC_SET64 && cmd != IPC_STAT64) {
		(void) __set_errno(EINVAL);
		return (-1);
	}
	return (syscall(SYS_shmsys, SHMCTL, shmid, cmd, buf));
}

int
shmxctl64(int shmid, int cmd, struct shmid_xds64 *buf)
{
	if (cmd != IPC_XSTAT64) {
		(void) __set_errno(EINVAL);
		return (-1);
	}
	return (syscall(SYS_shmsys, SHMCTL, shmid, cmd, buf));
}

int
shmdt(const void *shmaddr)
{
	return (syscall(SYS_shmsys, SHMDT, shmaddr));
}

int
shmget(key_t key, size_t size, int shmflg)
{
	return (syscall(SYS_shmsys, SHMGET, key, size, shmflg));
}

int
shmget_osm(key_t key, size_t size, int shmflg, size_t granule_sz)
{
	return (syscall(SYS_shmsys, SHMGET_OSM, key, size, shmflg, granule_sz));
}

int
shmids(int *buf, uint_t nids, uint_t *pnids)
{
	return (syscall(SYS_shmsys, SHMIDS, buf, nids, pnids));
}

int
shmadv(int shmid, uint_t cmd, uint_t *advice)
{
	return (syscall(SYS_shmsys, SHMADV, shmid, cmd, advice));
}

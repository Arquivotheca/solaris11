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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

#ifndef	_SYS_SHM_H
#define	_SYS_SHM_H

#include <sys/ipc.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 *	IPC Shared Memory Facility.
 */

/*
 *	Implementation Constants.
 */
#if (defined(_KERNEL) || defined(_KMEMUSER))

#define	SHMLBA	PAGESIZE	/* segment low boundary address multiple */
				/* (SHMLBA must be a power of 2) */
#else
#include <sys/unistd.h>		/* needed for _SC_PAGESIZE */
extern long _sysconf(int);	/* System Private interface to sysconf() */
#define	SHMLBA	(_sysconf(_SC_PAGESIZE))
#endif	/* defined(_KERNEL) || defined(_KMEMUSER)) */

/*
 *	Permission Definitions.
 */
#define	SHM_R	0400	/* read permission */
#define	SHM_W	0200	/* write permission */

/*
 *	Message Operation Flags.
 */
#define	SHM_RDONLY	010000	/* attach read-only (else read-write) */
#define	SHM_RND		020000	/* round attach address to SHMLBA */
#define	SHM_SHARE_MMU	040000  /* share VM resources such as page table */
#define	SHM_PAGEABLE	0100000 /* pageable ISM */

/*
 * Valid flags bits for shmat shmflag argument.
 */
#define	SHMAT_VALID_FLAGS_MASK		\
	(SHM_R | SHM_W | SHM_RDONLY | SHM_RND | SHM_SHARE_MMU | SHM_PAGEABLE)

typedef unsigned long shmatt_t;
/*
 * Flags for the shmid_ds's shm_flags field.
 */
#define	SHMDS_GRANULE	0x00000001	/* shm_gransize is valid */

/*
 *	Structure Definitions.
 */
struct shmid_ds {
	struct ipc_perm	shm_perm;	/* operation permission struct */
	size_t		shm_segsz;	/* size of segment in bytes */
	uintptr_t	shm_flags;	/* if 1 the shm_gransize is valid */
	ushort_t	shm_lkcnt;	/* number of times it is being locked */
	pid_t		shm_lpid;	/* pid of last shmop */
	pid_t		shm_cpid;	/* pid of creator */
	shmatt_t	shm_nattch;	/* number of attaches */
	ulong_t		shm_cnattch;	/* number of ISM attaches */
#if defined(_LP64)
	time_t		shm_atime;	/* last shmat time */
	time_t		shm_dtime;	/* last shmdt time */
	time_t		shm_ctime;	/* last change time */
	void		*shm_amp;	/* unused */
	uint64_t	shm_gransize;	/* granule size */
	uint64_t	shm_allocated;	/* mem allocated, for OSM */
	int64_t		shm_pad4[1];	/* reserve area */
#else	/* _LP64 */
	time_t		shm_atime;	/* last shmat time */
	int32_t		shm_pad1;	/* reserved for time_t expansion */
	time_t		shm_dtime;	/* last shmdt time */
	int32_t		shm_pad2;	/* reserved for time_t expansion */
	time_t		shm_ctime;	/* last change time */
	void		*shm_amp;	/* unused */
	uint64_t	shm_gransize;	/* granule size */
	uint64_t	shm_allocated;	/* mem allocated, for OSM */
#endif	/* _LP64 */
};

/*
 * Shared memory control operations
 */
#define	SHM_LOCK	3	/* Lock segment in core */
#define	SHM_UNLOCK	4	/* Unlock segment */

/*
 * Shared memory advice commands
 */
#define	SHM_ADV_GET		0	/* get advice */
#define	SHM_ADV_SET		1	/* set advice */

/*
 * Shared memory advice values
 */
#define	SHM_ACCESS_DEFAULT	0	/* default access */
#define	SHM_ACCESS_LWP		1	/* next thread will access heavily */
#define	SHM_ACCESS_MANY		2	/* many threads will access heavily */
#define	SHM_ACCESS_MANY_PSET	3	/* many threads in pset will access */
					/* heavily */


#if !defined(_KERNEL)
#if defined(__STDC__)
extern int shmget(key_t, size_t, int);
extern int shmget_osm(key_t, size_t, int, size_t);
extern int shmids(int *, uint_t, uint_t *);
extern int shmctl(int, int, struct shmid_ds *);
extern void *shmat(int, const void *, int);
extern int shmadv(int, uint_t, uint_t *);
#if (defined(_XPG3) && !defined(_XPG4)) || defined(__USE_LEGACY_PROTOTYPES__)
extern int shmdt(char *);
#else
extern int shmdt(const void *);
#endif /* (defined(_XPG3) && !defined(_XPG4)) || ... */

#else /* __STDC__ */

extern int shmget();
extern int shmget_osm();
extern int shmids();
extern int shmctl();
extern void *shmat();
extern int shmadv();
extern int shmdt();

#endif /* __STDC__ */
#endif /* !defined(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SHM_H */

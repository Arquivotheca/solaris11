/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_IMPL_H
#define	_MEMTEST_IMPL_H

#include <sys/types.h>
#include <sys/dditypes.h>
#include <sys/mutex.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	MEMTEST_RESERVATION_MAXNUM	5	/* max # of reservations */
#define	MEMTEST_RESERVATION_MAXSIZE	32768	/* max size of indiv res */

typedef struct memtest_rsrv {
	uint_t mr_id;
	caddr_t mr_bufaddr;
	size_t mr_bufsize;
	uint64_t mr_bufpaddr;
} memtest_rsrv_t;

typedef struct memtest {
	memtest_rsrv_t *mt_rsrvs;
	size_t mt_rsrv_maxsize;
	uint_t mt_rsrv_maxnum;
	uint_t mt_rsrv_lastid;
	uint_t mt_inject_maxnum;
	dev_info_t *mt_dip;
	uint_t mt_flags;
	kmutex_t mt_lock;
} memtest_t;

extern memtest_t memtest;

extern void memtest_dprintf(const char *, ...);
extern boolean_t memtest_dryrun(void);

extern int memtest_inject(intptr_t arg, int mode);
extern int memtest_reserve(intptr_t arg, int mode, int *);
extern int memtest_release(intptr_t arg);
extern void memtest_release_all(void);

#ifdef __cplusplus
}
#endif

#endif /* _MEMTEST_IMPL_H */

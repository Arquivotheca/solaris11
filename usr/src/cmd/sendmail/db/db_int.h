/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 1996, 1997, 1998
 *	Sleepycat Software.  All rights reserved.
 *
 *	@(#)db_int.h	10.77 (Sleepycat) 1/3/99
 */

#ifndef _DB_INTERNAL_H_
#define	_DB_INTERNAL_H_

#include "db.h"				/* Standard DB include file. */
#include "queue.h"
#include "shqueue.h"

/*******************************************************
 * General purpose constants and macros.
 *******************************************************/
#define	UINT16_T_MAX	    0xffff	/* Maximum 16 bit unsigned. */
#define	UINT32_T_MAX	0xffffffff	/* Maximum 32 bit unsigned. */

#define	DB_MIN_PGSIZE	0x000200	/* Minimum page size. */
#define	DB_MAX_PGSIZE	0x010000	/* Maximum page size. */

#define	DB_MINCACHE	10		/* Minimum cached pages */

#define	MEGABYTE	1048576

/*
 * If we are unable to determine the underlying filesystem block size, use
 * 8K on the grounds that most OS's use less than 8K as their VM page size.
 */
#define	DB_DEF_IOSIZE	(8 * 1024)

/*
 * Aligning items to particular sizes or in pages or memory.  ALIGNP is a
 * separate macro, as we've had to cast the pointer to different integral
 * types on different architectures.
 *
 * We cast pointers into unsigned longs when manipulating them because C89
 * guarantees that u_long is the largest available integral type and further,
 * to never generate overflows.  However, neither C89 or C9X  requires that
 * any integer type be large enough to hold a pointer, although C9X created
 * the intptr_t type, which is guaranteed to hold a pointer but may or may
 * not exist.  At some point in the future, we should test for intptr_t and
 * use it where available.
 */
#undef	ALIGNTYPE
#define	ALIGNTYPE		u_long
#undef	ALIGNP
#define	ALIGNP(value, bound)	ALIGN((ALIGNTYPE)value, bound)
#undef	ALIGN
#define	ALIGN(value, bound)	(((value) + (bound) - 1) & ~((bound) - 1))

/*
 * There are several on-page structures that are declared to have a number of
 * fields followed by a variable length array of items.  The structure size
 * without including the variable length array or the address of the first of
 * those elements can be found using SSZ.
 *
 * This macro can also be used to find the offset of a structure element in a
 * structure.  This is used in various places to copy structure elements from
 * unaligned memory references, e.g., pointers into a packed page.
 *
 * There are two versions because compilers object if you take the address of
 * an array.
 */
#undef	SSZ
#define SSZ(name, field)	((int)&(((name *)0)->field))

#undef	SSZA
#define SSZA(name, field)	((int)&(((name *)0)->field[0]))

/* Macros to return per-process address, offsets based on shared regions. */
#define	R_ADDR(base, offset)	((void *)((u_int8_t *)((base)->addr) + offset))
#define	R_OFFSET(base, p)	((u_int8_t *)(p) - (u_int8_t *)(base)->addr)

#define	DB_DEFAULT	0x000000	/* No flag was specified. */

/* Structure used to print flag values. */
typedef struct __fn {
	u_int32_t mask;			/* Flag value. */
	const char *name;		/* Flag name. */
} FN;

/* Set, clear and test flags. */
#define	F_SET(p, f)	(p)->flags |= (f)
#define	F_CLR(p, f)	(p)->flags &= ~(f)
#define	F_ISSET(p, f)	((p)->flags & (f))
#define	LF_SET(f)	(flags |= (f))
#define	LF_CLR(f)	(flags &= ~(f))
#define	LF_ISSET(f)	(flags & (f))

/*
 * Panic check:
 * All interfaces check the panic flag, if it's set, the tree is dead.
 */
#define	DB_PANIC_CHECK(dbp) {						\
	if ((dbp)->dbenv != NULL && (dbp)->dbenv->db_panic != 0)	\
		return (DB_RUNRECOVERY);				\
}

/* Display separator string. */
#undef	DB_LINE
#define	DB_LINE "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-="

/* Unused, or not-used-yet variable.  "Shut that bloody compiler up!" */
#define	COMPQUIET(n, v)	(n) = (v)

/*
 * Purify and similar run-time tools complain about unitialized reads/writes
 * for structure fields whose only purpose is padding.
 */
#define	UMRW(v)		(v) = 0

/*
 * Win16 needs specific syntax on callback functions.  Nobody else cares.
 */
#ifndef	DB_CALLBACK
#define	DB_CALLBACK	/* Nothing. */
#endif

/*******************************************************
 * Files.
 *******************************************************/
 /*
  * We use 1024 as the maximum path length.  It's too hard to figure out what
  * the real path length is, as it was traditionally stored in <sys/param.h>,
  * and that file isn't always available.
  */
#undef	MAXPATHLEN
#define	MAXPATHLEN	1024

#define	PATH_DOT	"."	/* Current working directory. */
#define	PATH_SEPARATOR	"/"	/* Path separator character. */

/*******************************************************
 * Mutex support.
 *******************************************************/
#include <sys/machlock.h>
typedef lock_t tsl_t;


/*
 * !!!
 * Various systems require different alignments for mutexes (the worst we've
 * seen so far is 16-bytes on some HP architectures).  The mutex (tsl_t) must
 * be first in the db_mutex_t structure, which must itself be first in the
 * region.  This ensures the alignment is as returned by mmap(2), which should
 * be sufficient.  All other mutex users must ensure proper alignment locally.
 */
#define	MUTEX_ALIGNMENT	sizeof(int)

/*
 * The offset of a mutex in memory.
 *
 * !!!
 * Not an off_t, so backing file offsets MUST be less than 4Gb.  See the
 * off field of the db_mutex_t as well.
 */
#define	MUTEX_LOCK_OFFSET(a, b)	((u_int32_t)((u_int8_t *)b - (u_int8_t *)a))

typedef struct _db_mutex_t {
#ifdef HAVE_SPINLOCKS
	tsl_t	  tsl_resource;		/* Resource test and set. */
#ifdef DIAGNOSTIC
	u_int32_t pid;			/* Lock holder: 0 or process pid. */
#endif
#else
	u_int32_t off;			/* Backing file offset. */
	u_int32_t pid;			/* Lock holder: 0 or process pid. */
#endif
	u_int32_t spins;		/* Spins before block. */
	u_int32_t mutex_set_wait;	/* Granted after wait. */
	u_int32_t mutex_set_nowait;	/* Granted without waiting. */
} db_mutex_t;

#include "mutex_ext.h"

/*******************************************************
 * Access methods.
 *******************************************************/
/* Lock/unlock a DB thread. */
#define	DB_THREAD_LOCK(dbp)						\
	if (F_ISSET(dbp, DB_AM_THREAD))					\
	    (void)__db_mutex_lock((db_mutex_t *)(dbp)->mutexp, -1);
#define	DB_THREAD_UNLOCK(dbp)						\
	if (F_ISSET(dbp, DB_AM_THREAD))					\
	    (void)__db_mutex_unlock((db_mutex_t *)(dbp)->mutexp, -1);

/*******************************************************
 * Environment.
 *******************************************************/
/* Type passed to __db_appname(). */
typedef enum {
	DB_APP_NONE=0,			/* No type (region). */
	DB_APP_DATA,			/* Data file. */
	DB_APP_LOG,			/* Log file. */
	DB_APP_TMP			/* Temporary file. */
} APPNAME;

/*******************************************************
 * Shared memory regions.
 *******************************************************/
/*
 * The shared memory regions share an initial structure so that the general
 * region code can handle races between the region being deleted and other
 * processes waiting on the region mutex.
 *
 * !!!
 * Note, the mutex must be the first entry in the region; see comment above.
 */
typedef struct _rlayout {
	db_mutex_t lock;		/* Region mutex. */
#define	DB_REGIONMAGIC	0x120897
	u_int32_t  valid;		/* Valid magic number. */
	u_int32_t  refcnt;		/* Region reference count. */
	size_t	   size;		/* Region length. */
	int	   majver;		/* Major version number. */
	int	   minver;		/* Minor version number. */
	int	   patch;		/* Patch version number. */
	int	   panic;		/* Region is dead. */
#define	INVALID_SEGID	-1
	int	   segid;		/* shmget(2) ID, or Win16 segment ID. */

#define	REGION_ANONYMOUS	0x01	/* Region is/should be in anon mem. */
	u_int32_t  flags;
} RLAYOUT;

/*
 * DB creates all regions on 4K boundaries out of sheer paranoia, so that
 * we don't make the underlying VM unhappy.
 */
#define	DB_VMPAGESIZE	(4 * 1024)
#define	DB_ROUNDOFF(n, round) {						\
	(n) += (round) - 1;						\
	(n) -= (n) % (round);						\
}

/*
 * The interface to region attach is nasty, there is a lot of complex stuff
 * going on, which has to be retained between create/attach and detach.  The
 * REGINFO structure keeps track of it.
 */
struct __db_reginfo;	typedef struct __db_reginfo REGINFO;
struct __db_reginfo {
					/* Arguments. */
	DB_ENV	   *dbenv;		/* Region naming info. */
	APPNAME	    appname;		/* Region naming info. */
	char	   *path;		/* Region naming info. */
	const char *file;		/* Region naming info. */
	int	    mode;		/* Region mode, if a file. */
	size_t	    size;		/* Region size. */
	u_int32_t   dbflags;		/* Region file open flags, if a file. */

					/* Results. */
	char	   *name;		/* Region name. */
	void	   *addr;		/* Region address. */
	int	    fd;			/* Fcntl(2) locking file descriptor.
					   NB: this is only valid if a regular
					   file is backing the shared region,
					   and mmap(2) is being used to map it
					   into our address space. */
	int	    segid;		/* shmget(2) ID, or Win16 segment ID. */
	void	   *wnt_handle;		/* Win/NT HANDLE. */

					/* Shared flags. */
/*				0x0001	COMMON MASK with RLAYOUT structure. */
#define	REGION_CANGROW		0x0002	/* Can grow. */
#define	REGION_CREATED		0x0004	/* Created. */
#define	REGION_HOLDINGSYS	0x0008	/* Holding system resources. */
#define	REGION_LASTDETACH	0x0010	/* Delete on last detach. */
#define	REGION_MALLOC		0x0020	/* Created in malloc'd memory. */
#define	REGION_PRIVATE		0x0040	/* Private to thread/process. */
#define	REGION_REMOVED		0x0080	/* Already deleted. */
#define	REGION_SIZEDEF		0x0100	/* Use default region size if exists. */
	u_int32_t   flags;
};

/*******************************************************
 * Mpool.
 *******************************************************/
/*
 * File types for DB access methods.  Negative numbers are reserved to DB.
 */
#define	DB_FTYPE_BTREE		-1	/* Btree. */
#define	DB_FTYPE_HASH		-2	/* Hash. */

/* Structure used as the DB pgin/pgout pgcookie. */
typedef struct __dbpginfo {
	size_t	db_pagesize;		/* Underlying page size. */
	int	needswap;		/* If swapping required. */
} DB_PGINFO;

/*******************************************************
 * Log.
 *******************************************************/
/* Initialize an LSN to 'zero'. */
#define	ZERO_LSN(LSN) {							\
	(LSN).file = 0;							\
	(LSN).offset = 0;						\
}

/* Return 1 if LSN is a 'zero' lsn, otherwise return 0. */
#define	IS_ZERO_LSN(LSN)	((LSN).file == 0)

/* Test if we need to log a change. */
#define	DB_LOGGING(dbc)							\
	(F_ISSET((dbc)->dbp, DB_AM_LOGGING) && !F_ISSET(dbc, DBC_RECOVER))

#ifdef DIAGNOSTIC
/*
 * Debugging macro to log operations.
 *	If DEBUG_WOP is defined, log operations that modify the database.
 *	If DEBUG_ROP is defined, log operations that read the database.
 *
 * D dbp
 * T txn
 * O operation (string)
 * K key
 * A data
 * F flags
 */
#define	LOG_OP(C, T, O, K, A, F) {					\
	DB_LSN _lsn;							\
	DBT _op;							\
	if (DB_LOGGING((C))) {						\
		memset(&_op, 0, sizeof(_op));				\
		_op.data = O;						\
		_op.size = strlen(O) + 1;				\
		(void)__db_debug_log((C)->dbp->dbenv->lg_info,		\
		    T, &_lsn, 0, &_op, (C)->dbp->log_fileid, K, A, F);	\
	}								\
}
#ifdef DEBUG_ROP
#define	DEBUG_LREAD(C, T, O, K, A, F)	LOG_OP(C, T, O, K, A, F)
#else
#define	DEBUG_LREAD(C, T, O, K, A, F)
#endif
#ifdef DEBUG_WOP
#define	DEBUG_LWRITE(C, T, O, K, A, F)	LOG_OP(C, T, O, K, A, F)
#else
#define	DEBUG_LWRITE(C, T, O, K, A, F)
#endif
#else
#define	DEBUG_LREAD(C, T, O, K, A, F)
#define	DEBUG_LWRITE(C, T, O, K, A, F)
#endif /* DIAGNOSTIC */

/*******************************************************
 * Transactions and recovery.
 *******************************************************/
/*
 * Out of band value for a lock.  The locks are returned to callers as offsets
 * into the lock regions.  Since the RLAYOUT structure begins all regions, an
 * offset of 0 is guaranteed not to be a valid lock.
 */
#define	LOCK_INVALID	0

/* The structure allocated for every transaction. */
struct __db_txn {
	DB_TXNMGR	*mgrp;		/* Pointer to transaction manager. */
	DB_TXN		*parent;	/* Pointer to transaction's parent. */
	DB_LSN		last_lsn;	/* Lsn of last log write. */
	u_int32_t	txnid;		/* Unique transaction id. */
	size_t		off;		/* Detail structure within region. */
	TAILQ_ENTRY(__db_txn) links;	/* Links transactions off manager. */
	TAILQ_HEAD(__kids, __db_txn) kids; /* Child transactions. */
	TAILQ_ENTRY(__db_txn) klinks;	/* Links child transactions. */

#define	TXN_MALLOC	0x01		/* Structure allocated by TXN system. */
	u_int32_t	flags;
};

/*******************************************************
 * Global variables.
 *******************************************************/
/*
 * !!!
 * Initialized in os/os_config.c, don't change this unless you change it
 * as well.
 */

struct __rmname {
	char *dbhome;
	int rmid;
	TAILQ_ENTRY(__rmname) links;
};

typedef struct __db_globals {
	int db_mutexlocks;		/* DB_MUTEXLOCKS */
	int db_pageyield;		/* DB_PAGEYIELD */
	int db_region_anon;		/* DB_REGION_ANON, DB_REGION_NAME */
	int db_region_init;		/* DB_REGION_INIT */
	int db_tsl_spins;		/* DB_TSL_SPINS */
					/* XA: list of opened environments. */
	TAILQ_HEAD(__db_envq, __db_env) db_envq;
					/* XA: list of id to dbhome mappings. */
	TAILQ_HEAD(__db_nameq, __rmname) db_nameq;
} DB_GLOBALS;

extern	DB_GLOBALS	__db_global_values;
#define	DB_GLOBAL(v)	__db_global_values.v

#include "os.h"
#include "os_ext.h"

#endif /* !_DB_INTERNAL_H_ */

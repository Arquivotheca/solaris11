/*
 * Copyright (c) 1994, 1998, Oracle and/or its affiliates. All rights reserved.
 */

/* Copyright 1991 NCR Corporation - Dayton, Ohio, USA */

/* Copyright (c) 1990 UNIX System Laboratories, Inc.		*/
/* Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989, 1990 AT&T	*/
/* All Rights Reserved						*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

#ifndef _NFS_LM_NLM_H
#define	_NFS_LM_NLM_H

#include <rpc/rpc.h>
#include <rpc/rpc_sztypes.h>

#ifdef	__cplusplus
extern "C" {
#endif


/*
 * This file was generated using rpcgen. Do not edit the file.
 *
 * Warning: this file is temporary and will go away.  Use
 * <rpcsvc/nlm_prot.h> instead.
 */

#define	LM_MAXSTRLEN		1024
#define	MAXXDRHOSTNAMELEN	LM_MAXSTRLEN+1

enum nlm_stats {
	nlm_granted = 0,
	nlm_denied = 1,
	nlm_denied_nolocks = 2,
	nlm_blocked = 3,
	nlm_denied_grace_period = 4,
	nlm_deadlck = 5
};
typedef enum nlm_stats nlm_stats;
bool_t		xdr_nlm_stats();

struct nlm_holder {
	bool_t		exclusive;
	int		svid;
	netobj		oh;
	uint_t		l_offset;
	uint_t		l_len;
};
typedef struct nlm_holder nlm_holder;
bool_t		xdr_nlm_holder();

struct nlm_testrply {
	nlm_stats	stat;
	union {
		struct nlm_holder holder;
	} nlm_testrply_u;
};
typedef struct nlm_testrply nlm_testrply;
bool_t		xdr_nlm_testrply();

struct nlm_stat {
	nlm_stats	stat;
};
typedef struct nlm_stat nlm_stat;
bool_t		xdr_nlm_stat();

struct nlm_res {
	netobj		cookie;
	nlm_stat	stat;
};
typedef struct nlm_res nlm_res;
bool_t		xdr_nlm_res();

struct nlm_testres {
	netobj		cookie;
	nlm_testrply	stat;
};
typedef struct nlm_testres nlm_testres;
bool_t		xdr_nlm_testres();

struct nlm_lock {
	char		*caller_name;
	netobj		fh;
	netobj		oh;
	int		svid;
	uint_t		l_offset;
	uint_t		l_len;
};
typedef struct nlm_lock nlm_lock;
bool_t		xdr_nlm_lock();

struct nlm_lockargs {
	netobj		cookie;
	bool_t		block;
	bool_t		exclusive;
	struct nlm_lock alock;
	bool_t		reclaim;
	int		state;
};
typedef struct nlm_lockargs nlm_lockargs;
bool_t		xdr_nlm_lockargs();

struct nlm_cancargs {
	netobj		cookie;
	bool_t		block;
	bool_t		exclusive;
	struct nlm_lock alock;
};
typedef struct nlm_cancargs nlm_cancargs;
bool_t		xdr_nlm_cancargs();

struct nlm_testargs {
	netobj		cookie;
	bool_t		exclusive;
	struct nlm_lock alock;
};
typedef struct nlm_testargs nlm_testargs;
bool_t		xdr_nlm_testargs();

struct nlm_unlockargs {
	netobj		cookie;
	struct nlm_lock alock;
};
typedef struct nlm_unlockargs nlm_unlockargs;
bool_t		xdr_nlm_unlockargs();

/*
 * The following enums are actually bit encoded for efficient boolean
 * algebra.... DON'T change them.....
 */

enum fsh_mode {
	fsm_DN = 0,
	fsm_DR = 1,
	fsm_DW = 2,
	fsm_DRW = 3
};
typedef enum fsh_mode fsh_mode;
bool_t		xdr_fsh_mode();

enum fsh_access {
	fsa_NONE = 0,
	fsa_R = 1,
	fsa_W = 2,
	fsa_RW = 3
};
typedef enum fsh_access fsh_access;
bool_t		xdr_fsh_access();

struct nlm_share {
	char		*caller_name;
	netobj		fh;
	netobj		oh;
	fsh_mode	mode;
	fsh_access	access;
};
typedef struct nlm_share nlm_share;
bool_t		xdr_nlm_share();

struct nlm_shareargs {
	netobj		cookie;
	nlm_share	share;
	bool_t		reclaim;
};
typedef struct nlm_shareargs nlm_shareargs;
bool_t		xdr_nlm_shareargs();

struct nlm_shareres {
	netobj		cookie;
	nlm_stats	stat;
	int		sequence;
};
typedef struct nlm_shareres nlm_shareres;
bool_t		xdr_nlm_shareres();

struct nlm_notify {
	char		*name;
	int		state;
};
typedef struct nlm_notify nlm_notify;
bool_t		xdr_nlm_notify();

#define	NLM_PROG	((uint_t)100021)
#define	NLM_VERS	((uint_t)1)
#define	NLM_TEST	((uint_t)1)
#define	NLM_LOCK	((uint_t)2)
#define	NLM_CANCEL	((uint_t)3)
#define	NLM_UNLOCK	((uint_t)4)
#define	NLM_GRANTED	((uint_t)5)
#define	NLM_TEST_MSG	((uint_t)6)
#define	NLM_LOCK_MSG	((uint_t)7)
#define	NLM_CANCEL_MSG	((uint_t)8)
#define	NLM_UNLOCK_MSG	((uint_t)9)
#define	NLM_GRANTED_MSG	((uint_t)10)
#define	NLM_TEST_RES	((uint_t)11)
#define	NLM_LOCK_RES	((uint_t)12)
#define	NLM_CANCEL_RES	((uint_t)13)
#define	NLM_UNLOCK_RES	((uint_t)14)
#define	NLM_GRANTED_RES	((uint_t)15)

/*
 * Private protocol for interacting with statd.  This has historically used
 * the same program number as the NLM protocol, but with an "unused"
 * version number.  The procedure numbers are chosen so as not to conflict
 * with the procedure numbers for NLM versions 1 and 3.
 */
#define	NLM_VERS2	((uint_t)2)
#define	PRV_CRASH	((uint_t)17)
#define	PRV_RECOVERY	((uint_t)18)

#define	NLM_VERS3	((uint_t)3)
#define	NLM_SHARE	((uint_t)20)
#define	NLM_UNSHARE	((uint_t)21)
#define	NLM_NM_LOCK	((uint_t)22)
#define	NLM_FREE_ALL	((uint_t)23)

/*
 * The following definitions have been taken from <rpcsvc/nlm_prot.h>
 */

enum nlm4_stats {
	NLM4_GRANTED = 0,
	NLM4_DENIED = 1,
	NLM4_DENIED_NOLOCKS = 2,
	NLM4_BLOCKED = 3,
	NLM4_DENIED_GRACE_PERIOD = 4,
	NLM4_DEADLCK = 5,
	NLM4_ROFS = 6,
	NLM4_STALE_FH = 7,
	NLM4_FBIG = 8,
	NLM4_FAILED = 9
};
typedef enum nlm4_stats nlm4_stats;

struct nlm4_holder {
	bool_t exclusive;
	int32 svid;
	netobj oh;
	uint64 l_offset;
	uint64 l_len;
};
typedef struct nlm4_holder nlm4_holder;

struct nlm4_testrply {
	nlm4_stats stat;
	union {
		struct nlm4_holder holder;
	} nlm4_testrply_u;
};
typedef struct nlm4_testrply nlm4_testrply;

struct nlm4_stat {
	nlm4_stats stat;
};
typedef struct nlm4_stat nlm4_stat;

struct nlm4_res {
	netobj cookie;
	nlm4_stat stat;
};
typedef struct nlm4_res nlm4_res;

struct nlm4_testres {
	netobj cookie;
	nlm4_testrply stat;
};
typedef struct nlm4_testres nlm4_testres;

struct nlm4_lock {
	char *caller_name;
	netobj fh;
	netobj oh;
	int32 svid;
	uint64 l_offset;
	uint64 l_len;
};
typedef struct nlm4_lock nlm4_lock;

struct nlm4_lockargs {
	netobj cookie;
	bool_t block;
	bool_t exclusive;
	struct nlm4_lock alock;
	bool_t reclaim;
	int32 state;
};
typedef struct nlm4_lockargs nlm4_lockargs;

struct nlm4_cancargs {
	netobj cookie;
	bool_t block;
	bool_t exclusive;
	struct nlm4_lock alock;
};
typedef struct nlm4_cancargs nlm4_cancargs;

struct nlm4_testargs {
	netobj cookie;
	bool_t exclusive;
	struct nlm4_lock alock;
};
typedef struct nlm4_testargs nlm4_testargs;

struct nlm4_unlockargs {
	netobj cookie;
	struct nlm4_lock alock;
};
typedef struct nlm4_unlockargs nlm4_unlockargs;
/*
 * The following enums are actually bit encoded for efficient
 * boolean algebra.... DON'T change them.....
 */

enum fsh4_mode {
	FSM_DN = 0,
	FSM_DR = 1,
	FSM_DW = 2,
	FSM_DRW = 3
};
typedef enum fsh4_mode fsh4_mode;

enum fsh4_access {
	FSA_NONE = 0,
	FSA_R = 1,
	FSA_W = 2,
	FSA_RW = 3
};
typedef enum fsh4_access fsh4_access;

struct nlm4_share {
	char *caller_name;
	netobj fh;
	netobj oh;
	fsh4_mode mode;
	fsh4_access access;
};
typedef struct nlm4_share nlm4_share;

struct nlm4_shareargs {
	netobj cookie;
	nlm4_share share;
	bool_t reclaim;
};
typedef struct nlm4_shareargs nlm4_shareargs;

struct nlm4_shareres {
	netobj cookie;
	nlm4_stats stat;
	int32 sequence;
};
typedef struct nlm4_shareres nlm4_shareres;

struct nlm4_notify {
	char *name;
	int32 state;
};
typedef struct nlm4_notify nlm4_notify;

#define	NLM4_VERS ((unsigned int)(4))

#define	NLMPROC4_NULL ((unsigned int)(0))
#define	NLMPROC4_TEST ((unsigned int)(1))
#define	NLMPROC4_LOCK ((unsigned int)(2))
#define	NLMPROC4_CANCEL ((unsigned int)(3))
#define	NLMPROC4_UNLOCK ((unsigned int)(4))
#define	NLMPROC4_GRANTED ((unsigned int)(5))
#define	NLMPROC4_TEST_MSG ((unsigned int)(6))
#define	NLMPROC4_LOCK_MSG ((unsigned int)(7))
#define	NLMPROC4_CANCEL_MSG ((unsigned int)(8))
#define	NLMPROC4_UNLOCK_MSG ((unsigned int)(9))
#define	NLMPROC4_GRANTED_MSG ((unsigned int)(10))
#define	NLMPROC4_TEST_RES ((unsigned int)(11))
#define	NLMPROC4_LOCK_RES ((unsigned int)(12))
#define	NLMPROC4_CANCEL_RES ((unsigned int)(13))
#define	NLMPROC4_UNLOCK_RES ((unsigned int)(14))
#define	NLMPROC4_GRANTED_RES ((unsigned int)(15))
#define	NLMPROC4_SHARE ((unsigned int)(20))
#define	NLMPROC4_UNSHARE ((unsigned int)(21))
#define	NLMPROC4_NM_LOCK ((unsigned int)(22))
#define	NLMPROC4_FREE_ALL ((unsigned int)(23))

/*
 * NLM_NUMRPCS can be used to size per-procedure arrays.
 */

#define	NLM_NUMRPCS	(NLMPROC4_FREE_ALL + 1)

/* xdr routines */
extern  bool_t xdr_nlm4_stats(XDR *, nlm4_stats*);
extern  bool_t xdr_nlm4_holder(XDR *, nlm4_holder*);
extern  bool_t xdr_nlm4_testrply(XDR *, nlm4_testrply*);
extern  bool_t xdr_nlm4_stat(XDR *, nlm4_stat*);
extern  bool_t xdr_nlm4_res(XDR *, nlm4_res*);
extern  bool_t xdr_nlm4_testres(XDR *, nlm4_testres*);
extern  bool_t xdr_nlm4_lock(XDR *, nlm4_lock*);
extern  bool_t xdr_nlm4_lockargs(XDR *, nlm4_lockargs*);
extern  bool_t xdr_nlm4_cancargs(XDR *, nlm4_cancargs*);
extern  bool_t xdr_nlm4_testargs(XDR *, nlm4_testargs*);
extern  bool_t xdr_nlm4_unlockargs(XDR *, nlm4_unlockargs*);
extern  bool_t xdr_fsh4_mode(XDR *, fsh4_mode*);
extern  bool_t xdr_fsh4_access(XDR *, fsh4_access*);
extern  bool_t xdr_nlm4_share(XDR *, nlm4_share*);
extern  bool_t xdr_nlm4_shareargs(XDR *, nlm4_shareargs*);
extern  bool_t xdr_nlm4_shareres(XDR *, nlm4_shareres*);
extern  bool_t xdr_nlm4_notify(XDR *, nlm4_notify*);

#ifdef __cplusplus
}
#endif

#endif /* _NFS_LM_NLM_H */

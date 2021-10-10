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
 * Network lock manager protocol definition
 * Copyright (c) 1986, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * Protocol used between local lock manager and remote lock manager.
 *
 * There are currently 3 versions of the protocol in use.  Versions 1
 * and 3 are used with NFS version 2.  Version 4 is used with NFS
 * version 3.
 *
 * (Note: there is also a version 2, but it defines an orthogonal set of
 * procedures that the status monitor uses to notify the lock manager of
 * changes in monitored systems.)
 */

#if RPC_HDR
%
%#include <rpc/rpc_sztypes.h>
%
#endif

#ifdef RPC_HDR
%#define LM_MAXSTRLEN	1024
%#define LM_MAXNAMELEN	(LM_MAXSTRLEN + 1)
#endif

/*
 * Types for versions 1 and 3.
 */

/*
 * Status of a call to the lock manager.  The lower case enums violate the
 * current style guide, but we're stuck with 'em.
 */

enum nlm_stats {
	nlm_granted = 0,
	nlm_denied = 1,
	nlm_denied_nolocks = 2,
	nlm_blocked = 3,
	nlm_denied_grace_period = 4,
	nlm_deadlck = 5
};

/*
 * The holder of a conflicting lock.
 */

struct nlm_holder {
	bool exclusive;
	int svid;
	netobj oh;
	unsigned l_offset;
	unsigned l_len;
};

union nlm_testrply switch (nlm_stats stat) {
	case nlm_denied:
		struct nlm_holder holder;
	default:
		void;
};

struct nlm_stat {
	nlm_stats stat;
};

struct nlm_res {
	netobj cookie;
	nlm_stat stat;
};

struct nlm_testres {
	netobj cookie;
	nlm_testrply stat;
};

struct nlm_lock {
	string caller_name<LM_MAXSTRLEN>;
	netobj fh;		/* identify a file */
	netobj oh;		/* identify owner of a lock */
	int svid;		/* generated from pid for svid */
	unsigned l_offset;
	unsigned l_len;
};

struct nlm_lockargs {
	netobj cookie;
	bool block;
	bool exclusive;
	struct nlm_lock alock;
	bool reclaim;		/* used for recovering locks */
	int state;		/* specify local status monitor state */
};

struct nlm_cancargs {
	netobj cookie;
	bool block;
	bool exclusive;
	struct nlm_lock alock;
};

struct nlm_testargs {
	netobj cookie;
	bool exclusive;
	struct nlm_lock alock;
};

struct nlm_unlockargs {
	netobj cookie;
	struct nlm_lock alock;
};

#ifdef RPC_HDR
%/*
% * The following enums are actually bit encoded for efficient
% * boolean algebra.... DON'T change them.....
% * The mixed-case enums violate the present style guide, but we're
% * stuck with 'em.
% */
#endif

enum	fsh_mode {
	fsm_DN  = 0,	/* deny none */
	fsm_DR  = 1,	/* deny read */
	fsm_DW  = 2,	/* deny write */
	fsm_DRW = 3	/* deny read/write */
};

enum	fsh_access {
	fsa_NONE = 0,	/* for completeness */
	fsa_R    = 1,	/* read only */
	fsa_W    = 2,	/* write only */
	fsa_RW   = 3	/* read/write */
};

struct	nlm_share {
	string caller_name<LM_MAXSTRLEN>;
	netobj	fh;
	netobj	oh;
	fsh_mode	mode;
	fsh_access	access;
};

struct	nlm_shareargs {
	netobj	cookie;
	nlm_share	share;
	bool	reclaim;
};

struct	nlm_shareres {
	netobj	cookie;
	nlm_stats	stat;
	int	sequence;
};

struct	nlm_notify {
	string name<LM_MAXNAMELEN>;
	int state;
};

/*
 * Types for version 4.
 *
 * This revision is designed to work with NFS V3.  The main changes from
 * NFS V2 to V3 that affect the NLM protocol are that all file offsets
 * and sizes are now unsigned 64-bit ints, and file handles are now
 * variable length.  In NLM V1 and V3, the fixed-length V2 file handle
 * was encoded as a 'netobj', which is a count followed by the data
 * bytes.  For NLM 4, the file handle is already a count followed by
 * data bytes, so the handle is copied directly into the netobj, rather
 * than being encoded with an additional byte count.
 */

/*
 * Status of a call to the lock manager.
 */

enum nlm4_stats {
	NLM4_GRANTED = 0,		/* lock was granted */
	NLM4_DENIED = 1,		/* lock was not granted, usually */
					/* due to conflicting lock */
	NLM4_DENIED_NOLOCKS = 2,	/* not granted: out of resources */
	NLM4_BLOCKED = 3,		/* not granted: expect callback */
					/* when granted */
	NLM4_DENIED_GRACE_PERIOD = 4,	/* not granted: server is */
					/* reestablishing old locks */
	NLM4_DEADLCK = 5,		/* not granted: deadlock detected */
	NLM4_ROFS = 6,			/* not granted: read-only filesystem */
	NLM4_STALE_FH = 7,		/* not granted: stale file handle */
	NLM4_FBIG = 8,			/* not granted: offset or length */
					/* too big */
	NLM4_FAILED = 9			/* not granted: some other error */
};

/*
 * The holder of a conflicting lock.
 */

struct nlm4_holder {
	bool exclusive;
	int32 svid;
	netobj oh;
	uint64 l_offset;
	uint64 l_len;
};

union nlm4_testrply switch (nlm4_stats stat) {
	case NLM4_DENIED:
		struct nlm4_holder holder;
	default:
		void;
};

struct nlm4_stat {
	nlm4_stats stat;
};

struct nlm4_res {
	netobj cookie;
	nlm4_stat stat;
};

struct nlm4_testres {
	netobj cookie;
	nlm4_testrply stat;
};

struct nlm4_lock {
	string caller_name<LM_MAXSTRLEN>;
	netobj fh;		/* identify a file */
	netobj oh;		/* identify owner of a lock */
	int32 svid;		/* generated from pid for svid */
	uint64 l_offset;
	uint64 l_len;
};

struct nlm4_lockargs {
	netobj cookie;
	bool block;
	bool exclusive;
	struct nlm4_lock alock;
	bool reclaim;		/* used for recovering locks */
	int32 state;		/* specify local status monitor state */
};

struct nlm4_cancargs {
	netobj cookie;
	bool block;
	bool exclusive;
	struct nlm4_lock alock;
};

struct nlm4_testargs {
	netobj cookie;
	bool exclusive;
	struct nlm4_lock alock;
};

struct nlm4_unlockargs {
	netobj cookie;
	struct nlm4_lock alock;
};

#ifdef RPC_HDR
%/*
% * The following enums are actually bit encoded for efficient
% * boolean algebra.... DON'T change them.....
% */
#endif

enum	fsh4_mode {
	FSM_DN  = 0,	/* deny none */
	FSM_DR  = 1,	/* deny read */
	FSM_DW  = 2,	/* deny write */
	FSM_DRW = 3	/* deny read/write */
};

enum	fsh4_access {
	FSA_NONE = 0,	/* for completeness */
	FSA_R    = 1,	/* read only */
	FSA_W    = 2,	/* write only */
	FSA_RW   = 3	/* read/write */
};

struct	nlm4_share {
	string caller_name<LM_MAXSTRLEN>;
	netobj	fh;
	netobj	oh;
	fsh4_mode	mode;
	fsh4_access	access;
};

struct	nlm4_shareargs {
	netobj	cookie;
	nlm4_share	share;
	bool	reclaim;
};

struct	nlm4_shareres {
	netobj	cookie;
	nlm4_stats	stat;
	int32	sequence;
};

struct	nlm4_notify {
	string name<LM_MAXNAMELEN>;
	int32 state;
};

/*
 * Over-the-wire protocol used between the network lock managers
 */

program NLM_PROG {
	version NLM_VERS {

		nlm_testres
			NLM_TEST(nlm_testargs) =		1;

		nlm_res
			NLM_LOCK(nlm_lockargs) =		2;

		nlm_res
			NLM_CANCEL(nlm_cancargs) =		3;

		nlm_res
			NLM_UNLOCK(nlm_unlockargs) =		4;
		/*
		 * remote lock manager call-back to grant lock
		 */
		nlm_res
			NLM_GRANTED(nlm_testargs) =		5;

		/*
		 * message passing style of requesting lock
		 */

		void
			NLM_TEST_MSG(nlm_testargs) =		6;
		void
			NLM_LOCK_MSG(nlm_lockargs) =		7;
		void
			NLM_CANCEL_MSG(nlm_cancargs) =		8;
		void
			NLM_UNLOCK_MSG(nlm_unlockargs) =	9;
		void
			NLM_GRANTED_MSG(nlm_testargs) =		10;
		void
			NLM_TEST_RES(nlm_testres) =		11;
		void
			NLM_LOCK_RES(nlm_res) =			12;
		void
			NLM_CANCEL_RES(nlm_res) =		13;
		void
			NLM_UNLOCK_RES(nlm_res) =		14;
		void
			NLM_GRANTED_RES(nlm_res) =		15;
	} = 1;

	version NLM_VERSX {
		nlm_shareres
			NLM_SHARE(nlm_shareargs) =		20;
		nlm_shareres
			NLM_UNSHARE(nlm_shareargs) =		21;
		nlm_res
			NLM_NM_LOCK(nlm_lockargs) =		22;
		void
			NLM_FREE_ALL(nlm_notify) =		23;
	} = 3;

	version NLM4_VERS {
		void
			NLMPROC4_NULL(void) =			0;
		nlm4_testres
			NLMPROC4_TEST(nlm4_testargs) =		1;
		nlm4_res
			NLMPROC4_LOCK(nlm4_lockargs) =		2;
		nlm4_res
			NLMPROC4_CANCEL(nlm4_cancargs) =	3;
		nlm4_res
			NLMPROC4_UNLOCK(nlm4_unlockargs) =	4;
		/*
		 * remote lock manager call-back to grant lock
		 */
		nlm4_res
			NLMPROC4_GRANTED(nlm4_testargs) =	5;

		/*
		 * message passing style of requesting lock
		 */

		void
			NLMPROC4_TEST_MSG(nlm4_testargs) =	6;
		void
			NLMPROC4_LOCK_MSG(nlm4_lockargs) =	7;
		void
			NLMPROC4_CANCEL_MSG(nlm4_cancargs) =	8;
		void
			NLMPROC4_UNLOCK_MSG(nlm4_unlockargs) =	9;
		void
			NLMPROC4_GRANTED_MSG(nlm4_testargs) =	10;
		void
			NLMPROC4_TEST_RES(nlm4_testres) =	11;
		void
			NLMPROC4_LOCK_RES(nlm4_res) =		12;
		void
			NLMPROC4_CANCEL_RES(nlm4_res) =		13;
		void
			NLMPROC4_UNLOCK_RES(nlm4_res) =		14;
		void
			NLMPROC4_GRANTED_RES(nlm4_res) =	15;

		/*
		 * DOS-style file sharing
		 */

		nlm4_shareres
			NLMPROC4_SHARE(nlm4_shareargs) =	20;
		nlm4_shareres
			NLMPROC4_UNSHARE(nlm4_shareargs) =	21;
		nlm4_res
			NLMPROC4_NM_LOCK(nlm4_lockargs) =	22;
		void
			NLMPROC4_FREE_ALL(nlm4_notify) =	23;
	} = 4;

} = 100021;

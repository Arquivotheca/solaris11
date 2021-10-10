/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_CTSMC_SEQ_H
#define	_SYS_CTSMC_SEQ_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	SMC_SEQN_BUSY		-1
#define	SMC_BLOCK_SZ		16
#define	MAX_SMC_MINORS		256
#define	NUM_BLOCKS			(MAX_SMC_MINORS/SMC_BLOCK_SZ)
#define	SMC_MAX_SEQ		0x100
#define	REQ_Q_SZ	8

/*
 * There need to be correlation between a queue pair and a sequence.
 * We need to maintain a list containing outstanding request messages
 * mapping of <seq, cmd> <--> <minor-num, user_seq>. There may
 * be multiple messages outstanding on same sequence number. We
 * maintain an array of size 256, each entry containing upto 8
 * outstanding messages.
 * We keep a counter for each entry which is decremented every
 * 90 sec.(default) by a garbage collector thread. If a response
 * isn't received by then, this entry is freed.
 */
typedef struct {
	uint8_t	cmd;
	uint8_t minor;
	uint8_t sequence;
	uint8_t	buf_idx;	/* Buffer index in case of synchronous req */
	int8_t	counter;	/* Expiry timer, ent freed if this reaches 0 */
} ctsmc_qent_t;

/*
 * SMC request queue entry. There is one for each sequence number from
 * 0...255. Following entry will be reserved for each sequence number.
 * There will be an array of size 256 for each of these entries
 */
typedef struct {
	uint8_t		bitmask; /* Bit mask to indicate occupied entries */
	ctsmc_qent_t	req_Q[REQ_Q_SZ]; /* Upto 8 unique outstanding req */
} ctsmc_reqent_t;

/*
 * Queue up internal entries waiting on sequence 0
 */
typedef struct {
	uint16_t	seq0_cmdmsk[NUM_BLOCKS]; /* Mask, which cmds are q'd */
	kmutex_t	seq0_lock;
	kcondvar_t	seq0_cv;
} ctsmc_seq0_ent_t;

/*
 * 'clients_waiting' is incremented whenever a free sequence# can't
 * be allocated. seqcond will be signalled when a seq # is available
 * (e.g. a response is received). Since sequence number 0 is used to
 * identify asynchronous messages, 1...255 will comprise valid sequence
 * numbers.  Driver will need to flow control anytime a command can
 * not be placed in request array because all sequence numbers are
 * filled up completely.
 */
typedef struct {
	kmutex_t	lock;		/* Protect sequence number array */
	kmutex_t	reqlock;	/* Mutex for reqcond */
	kcondvar_t	reqcond[NUM_BLOCKS]; /* condvar for seq%NUM_BLOCKS */
	uint8_t		waitflag;		/* for waiting on condvar */
	ctsmc_queue_t	queue;			/* Queue of sequence numbers */
	uint16_t	mask[NUM_BLOCKS];	/* Bit mask of allocation */
	ctsmc_reqent_t ctsmc_req_Arr[SMC_MAX_SEQ]; /* list of seq# allocation */
	ctsmc_seq0_ent_t	seq0_ent;	/* Seq0 alloc list */
} ctsmc_seqarr_t;

#define	SET_BUF_INDEX(SMC, seQ, poS, indeX, cnT)	\
do {	\
	ctsmc_seqarr_t *lisT = (SMC)->ctsmc_seq_list;	\
	ctsmc_reqent_t *reqEnt =				\
		&lisT->ctsmc_req_Arr[seQ];		\
	LOCK_DATA(lisT);			\
	reqEnt->req_Q[poS].buf_idx = indeX;	\
	UNLOCK_DATA(lisT);			\
} while (cnT)

#define	GET_BUF_INDEX(SMC, seQ, poS, indeX, cnT)	\
do {	\
	ctsmc_reqent_t *reqEnt =					\
		&(SMC)->ctsmc_seq_list->ctsmc_req_Arr[seQ];	\
		indeX = reqEnt->req_Q[poS].buf_idx;	\
} while (cnT)

/*
 * Wait on appropriate condition variable
 */
#define	SEQ_WAIT(SMC, seq, pos, tout, ret, cnT)	\
do {									\
	ctsmc_seqarr_t *seqarr = (SMC)->ctsmc_seq_list;	\
	ctsmc_reqent_t *reqent = &seqarr->ctsmc_req_Arr[seq];		\
	int cvRet;		\
	ret = SMC_SUCCESS;						\
									\
	LOCK_DATA(seqarr);				\
	while ((reqent->bitmask) & (1 << (pos))) {			\
		cvRet = cv_timedwait_sig(&seqarr->reqcond[seq%SMC_BLOCK_SZ], \
				&seqarr->lock, (tout)); \
		if (cvRet == 0)					\
			ret = SMC_OP_ABORTED;		\
		else	\
		if (cvRet == -1)		\
			ret = SMC_TIMEOUT;	\
		else	\
			ret = SMC_SUCCESS;	\
		if (ret != SMC_SUCCESS)	\
			break;	\
	}								\
	UNLOCK_DATA(seqarr);			\
} while (cnT)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CTSMC_SEQ_H */

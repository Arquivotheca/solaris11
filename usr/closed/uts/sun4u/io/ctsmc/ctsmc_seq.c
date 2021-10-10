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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * ctsmc_seq - sequence space management routines
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/log.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/kmem.h>

#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/strsun.h>
#include <sys/poll.h>

#include <sys/debug.h>

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>

#include <sys/inttypes.h>
#include <sys/ksynch.h>

#include <sys/ctsmc_debug.h>
#include <sys/ctsmc.h>

/*
 * The following variable is a count of the number of
 * request messages currently outstanding in SMC
 */
static	uint16_t ctsmc_num_outstanding_req = 0;
static	uint64_t ctsmc_cnt_freeseq = 0;
static	uint64_t ctsmc_seq_timer_cnt = 0;

/*
 * The following specifies the interval when the
 * timeout handler is invoked, in seconds
 */
static int	ctsmc_timeout_interval_s = 60;

/*
 * This is the counter. If a sequence misses these
 * count of timeouts and still not freed, it's
 * freed by the timeout handler
 */
static int	ctsmc_max_timer_count = 5;

/*
 * Find position of lowest significant 0 in an unsigned number
 */
static int
ctsmc_find_ls0(uint16_t num)
{
	int j;

	for (j = 0; (num&1) == 1; num >>= 1, j++)
		;

	return (j);
}

/*
 * Initialize SMC sequence number management array
 */
void
ctsmc_init_seqlist(ctsmc_state_t *ctsmc)
{
	ctsmc_seqarr_t *list;
	int i, j;
	ctsmc_reqent_t *reqHd;
	ctsmc_qent_t *req;
	ctsmc_hwinfo_t *smchw = ctsmc->ctsmc_hw;

	list = NEW(1, ctsmc_seqarr_t);
	ctsmc->ctsmc_seq_list = list;
	reqHd = list->ctsmc_req_Arr;

	mutex_init(&list->lock, "SMC Sequence Lock",
	    MUTEX_DRIVER, (void *)smchw->smh_ib);
	for (j = 0; j < NUM_BLOCKS; j++)
		cv_init(&list->reqcond[j], NULL,
		    CV_DRIVER, NULL);
	list->waitflag = 0;

	/*
	 * Initialize structure for sequence number 0
	 */
	mutex_init(&list->seq0_ent.seq0_lock, "SMC Seq0",
	    MUTEX_DRIVER, (void *)smchw->smh_ib);
	cv_init(&list->seq0_ent.seq0_cv, NULL, CV_DRIVER, NULL);
	for (i = 0; i < NUM_BLOCKS; i++)
		list->seq0_ent.seq0_cmdmsk[i] = 0;

	/*
	 * Initialize queue with sequence numbers 1...255
	 */
	for (i = 0; i < NUM_BLOCKS; i++)
		list->mask[i] = 0;
	ctsmc_initQ(&list->queue, SMC_MAX_SEQ - 1,
	    SMC_MAX_SEQ - 1, 1, 1);
	for (i = 0; i < SMC_MAX_SEQ; i++) {
		reqHd[i].bitmask = 0; /* No entry occupied */
		req = reqHd[i].req_Q;
		for (j = 0; j < REQ_Q_SZ; j++) {
			req[j].cmd = 0;
			req[j].minor = 0;
			req[j].sequence = 0;
			req[j].buf_idx = 0;
			req[j].counter = 0;
		}
	}
}

/*
 * Free sequence number allocation array
 */
void
ctsmc_free_seqlist(ctsmc_state_t *ctsmc)
{
	int i;
	ctsmc_seqarr_t *list =  ctsmc->ctsmc_seq_list;

	mutex_destroy(&list->seq0_ent.seq0_lock);
	cv_destroy(&list->seq0_ent.seq0_cv);
	for (i = 0; i < NUM_BLOCKS; i++)
		cv_destroy(&list->reqcond[i]);
	mutex_destroy(&list->lock);

	ctsmc_freeQ(&list->queue);
	FREE(list, 1, ctsmc_seqarr_t);
	ctsmc->ctsmc_seq_list = NULL;
}

/*
 * Allocate a placeholder for a new request message to be sent.
 * The offset of the request in the seq# is returned in pos.
 * This will be used by the waiter to know when a response is
 * received. Sequence number 0 is not allocated and is reserved.
 * Both internal and external clients will compete for same
 * sequence number space.
 */
uint8_t
ctsmc_getSeq(ctsmc_state_t *ctsmc, uint8_t minor, uint8_t uSequence,
		uint8_t cmd, uint8_t *seq, uint8_t *pos)
{
	ctsmc_seqarr_t *list = ctsmc->ctsmc_seq_list;
	ctsmc_reqent_t *ent;
	ctsmc_qent_t *req;
	int i, stop;
	uint8_t	j, mask, bitpos = 0;
	uint8_t skip = B_FALSE;	/* Whether seq# to be skipped */
	ctsmc_queue_t *queue = &list->queue;

	LOCK_DATA(list);

	/*
	 * If a free sequence number can not be obtained, traverse the
	 * entire queue, starting from front to end - to check if this
	 * request can be queued up. Any sequence number where an
	 * identical command is not already queued up can be chosen
	 */
	if (ctsmc_deQ(queue, 1, seq) == B_TRUE) {
		ent = &list->ctsmc_req_Arr[*seq];
		req = ent->req_Q;
	} else {
		i = qFront(queue), stop = qEnd(queue);
		do {
			skip = B_TRUE;
			*seq = qEntry(queue)[i];
			ent = &list->ctsmc_req_Arr[*seq];

			if (i >= qSize(queue)) { /* Went past end */
				i = 0;
				continue;
			}

			/*
			 * If all entries are occupied for this sequence,
			 * skip it
			 */
			if ((mask = ent->bitmask) == (uint8_t)-1) {
				i++;
				continue;
			}

			/*
			 * Slot available, check whether this command can be
			 * queued. We need to check all occupied entries to
			 * find if an identical request command is already
			 * queued, and if so skip this sequence number.
			 */
			skip = B_FALSE;
			req = ent->req_Q;
			for (j = 0; mask; mask >>= 1, j++) {
				if ((mask & 1) && (req[j].cmd == cmd)) {
					skip = B_TRUE;
					break;
				}
			}

			if (skip == B_TRUE) {
				i++;
				continue;
			}

			/*
			 * S U C C E S S !!!
			 */
			break;
		} while (i != stop);
	}

	if (skip == B_FALSE) {
		bitpos = ctsmc_find_ls0(ent->bitmask);
		SETBIT(ent->bitmask, bitpos);
		req[bitpos].cmd = cmd;
		req[bitpos].minor = minor;
		req[bitpos].sequence = uSequence;
		req[bitpos].buf_idx = 0;
		req[bitpos].counter = SMC_SEQN_BUSY;

		SETBIT(list->mask[NUM_TO_BLOCK(*seq)], NUM_OFFSET(*seq));

		*pos = bitpos;
		ctsmc_num_outstanding_req++;
	}

	UNLOCK_DATA(list);

	/*
	 * If no sequence number is available, return failure and driver
	 * should flow control. We may instead wait here and wake up
	 * when a response is received
	 */

#ifdef DEBUG
	if (skip == B_TRUE) {
		SMC_DEBUG3(SMC_UTILS_DEBUG, "ctsmc_getSeq() failed: "
		    "msgid = %x, cmd = %x, "
		    "minor = %x", uSequence, cmd, minor);
	}
#endif	/* DEBUG */

	return (skip == B_TRUE ? SMC_FAILURE : SMC_SUCCESS);
}

/*
 * Reserve a slot in seq# 0
 */
uint8_t
ctsmc_getSeq0(ctsmc_state_t *ctsmc, uint8_t minor, uint8_t uSequence,
		uint8_t cmd, uint8_t *pos)
{
	ctsmc_seqarr_t *list = ctsmc->ctsmc_seq_list;
	ctsmc_reqent_t *ent;
	ctsmc_qent_t *req;
	uint8_t mask, bitpos;
	uint8_t block = NUM_TO_BLOCK(cmd), off = NUM_OFFSET(cmd);
	int i;

	LOCK_DATA(list);
	ent = &list->ctsmc_req_Arr[0];
	req = ent->req_Q;

	/*
	 * Check whether the particular command is already queued
	 * up and if so, wait
	 */
	mutex_enter(&list->seq0_ent.seq0_lock);
	while (BITTEST(list->seq0_ent.seq0_cmdmsk[block], off)) {
		cv_wait(&list->seq0_ent.seq0_cv,
		    &list->seq0_ent.seq0_lock);
	}
	SETBIT(list->seq0_ent.seq0_cmdmsk[block], off);
	mutex_exit(&list->seq0_ent.seq0_lock);

	/*
	 * Now search list for this sequence number with
	 * matching command
	 */

	mask = ent->bitmask;
	for (i = 0; mask; mask >>= 1, i++) {
		if ((mask & 1) && (req[i].cmd == cmd)) {	/* skip it */
			UNLOCK_DATA(list);
			return (SMC_FAILURE);
		}
	}

	bitpos = ctsmc_find_ls0(ent->bitmask);

	SETBIT(ent->bitmask, bitpos);
	req[bitpos].cmd = cmd;
	req[bitpos].minor = minor;
	req[bitpos].sequence = uSequence;
	req[bitpos].buf_idx = 0;
	req[bitpos].counter = SMC_SEQN_BUSY;

	SETBIT(list->mask[0], 0);

	*pos = bitpos;
	ctsmc_num_outstanding_req++;

	UNLOCK_DATA(list);

	return (SMC_SUCCESS);
}

/*
 * Match a response message with a request message
 */
uint8_t
ctsmc_findSeq(ctsmc_state_t *ctsmc, uint8_t seq, uint8_t cmd,
		uint8_t *minor, uint8_t *uSequence, uint8_t *pos)
{
	ctsmc_seqarr_t *list = ctsmc->ctsmc_seq_list;
	ctsmc_reqent_t *ent;
	ctsmc_qent_t *req;
	uint8_t found = B_FALSE, mask;
	int i;

	ent = &list->ctsmc_req_Arr[seq];
	req = ent->req_Q;

	/*
	 * Search list for this sequence number with matching command.
	 * No lock is held during search.
	 */
	mask = ent->bitmask;
	for (i = 0; mask; mask >>= 1, i++) {
		if ((mask & 1) && (req[i].cmd == cmd)) {
			found = B_TRUE;
			break;
		}
	}
	/*
	 * If a match was found, return to the caller requestor's minor,
	 * sequence numner and position in the per sequence queue.
	 */
	if (found == B_TRUE) {
		*minor = req[i].minor;
		*uSequence = req[i].sequence;
		*pos = i;
	}

	if (found == B_TRUE)
		return (SMC_SUCCESS);
	else
		return (SMC_FAILURE);
}

/*
 * Once a message is received, this routine is used to free
 * the sequence number from request message queue, and return
 * minor and sequence numbers for this response message.
 */
void
ctsmc_freeSeq(ctsmc_state_t *ctsmc, uint8_t seq, uint8_t pos)
{

	ctsmc_seqarr_t *list = ctsmc->ctsmc_seq_list;
	ctsmc_reqent_t *ent = &list->ctsmc_req_Arr[seq];
	uint8_t cmd = ent->req_Q[pos].cmd;

	/*
	 * Before we make any attempt to free this sequence, make
	 * sure that it's not already been freed for some reason.
	 */
	if (!BITTEST(list->mask[NUM_TO_BLOCK(seq)], NUM_OFFSET(seq)))
			return;

	LOCK_DATA(list);

	if (BITTEST(ent->bitmask, pos)) {
		ent->req_Q[pos].minor = 0;
		ent->req_Q[pos].sequence = 0;
		ent->req_Q[pos].cmd = 0;
		ent->req_Q[pos].buf_idx = 0;
		ent->req_Q[pos].counter = 0;
		CLRBIT(ent->bitmask, pos);
		ctsmc_num_outstanding_req--;
	} else {
		UNLOCK_DATA(list);
		return;
	}

	/*
	 * If there is no response outstanding on this sequence, return
	 * to queue
	 */
	if (seq && (ent->bitmask == 0)) {
		(void) ctsmc_enQ(&list->queue, 1, &seq);
		CLRBIT(list->mask[NUM_TO_BLOCK(seq)], NUM_OFFSET(seq));
	}

	/*
	 * For sequence# 0, additionally unmask the command
	 */
	if (seq == 0) {
		uint8_t block = NUM_TO_BLOCK(cmd), off = NUM_OFFSET(cmd);
		mutex_enter(&list->seq0_ent.seq0_lock);
		CLRBIT(list->seq0_ent.seq0_cmdmsk[block], off);
		mutex_exit(&list->seq0_ent.seq0_lock);
	}

	UNLOCK_DATA(list);
}

/*
 * Initialize the counter for an entry from SMC_SEQN_BUSY to
 * ctsmc_max_timer_count
 */
int
ctsmc_init_seq_counter(ctsmc_state_t *ctsmc, ctsmc_reqpkt_t *req)
{
	uint8_t pos, minor, uSeq;
	ctsmc_seqarr_t *list = ctsmc->ctsmc_seq_list;
	ctsmc_reqent_t *ent = &list->ctsmc_req_Arr[SMC_MSG_SEQ(req)];

	if (ctsmc_findSeq(ctsmc, SMC_MSG_SEQ(req), SMC_MSG_CMD(req),
	    &minor, &uSeq, &pos) != SMC_SUCCESS)
		return (SMC_FAILURE);

	LOCK_DATA(list);
	ent->req_Q[pos].counter = ctsmc_max_timer_count;
	UNLOCK_DATA(list);

	return (SMC_SUCCESS);
}

/*
 * When a minor/sequence is deleted, we should remove any entry from
 * sequence number array if present because a response was probably
 * never received
 */
void
ctsmc_clear_seq_refs(ctsmc_state_t *ctsmc, uint8_t minor)
{
	ctsmc_seqarr_t *list = ctsmc->ctsmc_seq_list;
	int i, j, k;
	ctsmc_reqent_t *ent;
	ctsmc_queue_t *queue = &list->queue;
	uint8_t seq, entmsk;
	uint16_t mask, *m_mask = list->mask;
	ctsmc_hwinfo_t *m = ctsmc->ctsmc_hw;

	LOCK_DATA(list);

	for (i = 0; i < NUM_BLOCKS; i++) {
		mask = m_mask[i];
		for (j = 0; mask; mask >>= 1, j++) {
			if (mask&1) {
				seq = i * SMC_BLOCK_SZ + j;
				ent = &list->ctsmc_req_Arr[seq];
				entmsk = ent->bitmask;
				for (k = 0; entmsk; entmsk >>= 1,  k++) {
					if ((ent->req_Q[k].minor == minor) &&
					    (ent->req_Q[k].cmd != 0) &&
					    BITTEST(ent->bitmask, k)) {
						ctsmc_freeBuf(ctsmc,
						    ent->req_Q[k].buf_idx);
						ent->req_Q[k].minor = 0;
						ent->req_Q[k].sequence = 0;
						ent->req_Q[k].cmd = 0;
						ent->req_Q[k].buf_idx = 0;
						ent->req_Q[k].counter = 0;

						CLRBIT(ent->bitmask, k);
					}
				}
				/*
				 * If no request is queued in this entry,
				 * return this sequence number
				 */
				if (ent->bitmask == 0) {
					ctsmc_num_outstanding_req--;
					if (m->smh_smc_num_pend_req)
						m->smh_smc_num_pend_req--;
					if (seq)
						(void) ctsmc_enQ(queue, 1,
						    &seq);
					CLRBIT(m_mask[i], j);
				}
			}
		}
	}

	UNLOCK_DATA(list);
}

/*
 * This function will run every 90 sec (configurable) and decrement
 * the counter for every allocated sequence entry. If a counter
 * reaches 0, corresponding entry is freed
 */
static void
ctsmc_sequence_timer(ctsmc_state_t *ctsmc)
{
	ctsmc_seqarr_t *list = ctsmc->ctsmc_seq_list;
	int i, j, k;
	ctsmc_reqent_t *ent;
	ctsmc_queue_t *queue = &list->queue;
	uint8_t seq, entmsk;
	uint16_t mask, *m_mask = list->mask;
	ctsmc_hwinfo_t *m = ctsmc->ctsmc_hw;

	/*
	 * If no sequence number is being used, return
	 */
	if (qFull(queue))
		return;

	LOCK_DATA(list);
	ctsmc_seq_timer_cnt++;
	for (i = 0; i < NUM_BLOCKS; i++) {
		mask = m_mask[i];
		for (j = 0; mask; mask >>= 1, j++) {
			if ((mask&1) == 0)
				continue;
			seq = i * SMC_BLOCK_SZ + j;
			ent = &list->ctsmc_req_Arr[seq];
			entmsk = ent->bitmask;
			for (k = 0; entmsk; entmsk >>= 1,  k++) {
				if (BITTEST(ent->bitmask, k) &&
				    (ent->req_Q[k].cmd != 0) &&
				    (ent->req_Q[k].counter > 0)) {
					ent->req_Q[k].counter--;
					if (ent->req_Q[k].counter <= 0) {
						SMC_DEBUG4(SMC_UTILS_DEBUG,
						    "?Freeing Ent:"
						    " SMC Seq = %d, App "
						    "Seq = %d, minor = %d,"
						    " cmd = 0x%x", seq,
						    ent->req_Q[k].sequence,
						    ent->req_Q[k].minor,
						    ent->req_Q[k].cmd);
						ctsmc_freeBuf(ctsmc,
						    ent->req_Q[k].buf_idx);
						ent->req_Q[k].minor = 0;
						ent->req_Q[k].sequence = 0;
						ent->req_Q[k].cmd = 0;
						ent->req_Q[k].buf_idx = 0;
						ent->req_Q[k].counter = 0;

						CLRBIT(ent->bitmask, k);
						ctsmc_cnt_freeseq++;
					}
				}
			}
			/*
			 * If no request is queued in this entry,
			 * return this sequence number
			 */
			if (ent->bitmask == 0) {
				SMC_DEBUG(SMC_UTILS_DEBUG, "?Freed SMC "
				    "Seq# %d", seq);
				if (seq)
					(void) ctsmc_enQ(queue, 1, &seq);
				CLRBIT(m_mask[i], j);
				ctsmc_num_outstanding_req--;
				if (m->smh_smc_num_pend_req)
					m->smh_smc_num_pend_req--;
			}
		}
	}

	UNLOCK_DATA(list);
}

/*
 * Handler function which gets invoked every 90 sec. to
 * clean up resources for lost packet.
 */
void
ctsmc_sequence_timeout_handler(void *arg)
{
	ctsmc_state_t *ctsmc = (ctsmc_state_t *)arg;
	clock_t intval = drv_usectohz(ctsmc_timeout_interval_s * MICROSEC);

	LOCK_DATA(ctsmc);
	/*
	 * If SMC is detaching, signal and return
	 */
	if (ctsmc->ctsmc_init & SMC_IS_DETACHING) {
		cv_signal(&ctsmc->exit_cv);
		UNLOCK_DATA(ctsmc);
		return;
	}

	ctsmc->ctsmc_flag |= SMC_SEQ_TIMEOUT_RUNNING;
	UNLOCK_DATA(ctsmc);

	/*
	 * Invoke the sequence retriever routine without holding
	 * the mutex
	 */
	ctsmc_sequence_timer(ctsmc);

	LOCK_DATA(ctsmc);
	ctsmc->ctsmc_flag &= ~SMC_SEQ_TIMEOUT_RUNNING;
	/*
	 * Before exiting, again make sure that SMC is not detaching.
	 * If SMC is detaching, signal and exit
	 */
	if (ctsmc->ctsmc_init & SMC_IS_DETACHING) {
		cv_signal(&ctsmc->exit_cv);
	} else {
		/*
		 * Reschedule timeout for this function to run again
		 */
		ctsmc->ctsmc_tid = timeout(ctsmc_sequence_timeout_handler,
		    ctsmc, intval);
	}

	UNLOCK_DATA(ctsmc);
}

void
ctsmc_wait_until_seq_timeout_exits(ctsmc_state_t *ctsmc)
{
	clock_t tout = drv_usectohz(10 * MICROSEC);

	LOCK_DATA(ctsmc);

	/*
	 * Try to stop timeout; this will happen if the timer is
	 * not already running
	 */
	if (ctsmc->ctsmc_tid) {
		(void) untimeout(ctsmc->ctsmc_tid);
		ctsmc->ctsmc_tid = 0;
	}

	if (ctsmc->ctsmc_flag & SMC_SEQ_TIMEOUT_RUNNING)
		(void) cv_reltimedwait_sig(&ctsmc->exit_cv, &ctsmc->lock,
		    tout, TR_CLOCK_TICK);

	UNLOCK_DATA(ctsmc);
}

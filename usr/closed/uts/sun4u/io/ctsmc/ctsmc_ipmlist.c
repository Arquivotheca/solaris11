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

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ctsmc_ipmlist - IPMI sequence list management routines
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

#include <sys/ctsmc.h>

/* *** SMC DRIVER IPMI SEQUENCE SPACE MANAGEMENT ROUTINES *** */

extern int ctsmc_detect_duplicate(int len, uchar_t *list, intFnPtr iFn);

/*
 * Initialize an array of sequence numbers
 */
static void
ctsmc_initIPMSeqArr(ctsmc_ipmi_seq_t *list)
{
	list->smseq_mask = 0;
	ctsmc_initQ(&list->smseq_queue, SMC_IMAX_SEQ, SMC_IMAX_SEQ, 0, 1);
	mutex_init(&list->lock, NULL, MUTEX_DRIVER,
			NULL);
}

/*
 * Frees an array of sequence numbers
 */
static void
ctsmc_freeIPMSeqArr(ctsmc_ipmi_seq_t *list)
{
	list->smseq_mask = 0;
	ctsmc_freeQ(&list->smseq_queue);
	mutex_destroy(&list->lock);
}

/*
 * Reserves a set of sequence numbers for a minor node
 */
static int
ctsmc_reserveSeq(ctsmc_ipmi_seq_t *list, uint8_t minor, uint8_t count,
		uint8_t *seqL)
{
	int i;

	LOCK_DATA(list);

	/*
	 * Make sure request is valid and we can meet the request
	 */
	if (ctsmc_deQ(&list->smseq_queue, count, seqL) != B_TRUE) {
		UNLOCK_DATA(list);
		return (SMC_FAILURE);
	}

	for (i = 0; i < count; i++) {
		/*
		 * Mark minor for each of these sequence numbers
		 * and set mask for each.
		 */
		list->smseq_minor[seqL[i]] = minor;
		list->smseq_mask |= ((uint64_t)1 << seqL[i]);
	}
	UNLOCK_DATA(list);

	return (SMC_SUCCESS);
}

/*
 * Returns a set of free sequence numbers
 */
static int
ctsmc_unreserveIPMSeq(ctsmc_ipmi_seq_t *list, uint8_t minor,
		uint8_t count, uint8_t *seqL)
{
	int i;

	/*
	 * Check all these sequence numbers are indeed allocated,
	 * and this minor number is the rightful owner of
	 * all of them
	 */
	for (i = 0; i < count; i++) {
		if (!(list->smseq_mask & ((uint64_t)1 << seqL[i])) ||
			list->smseq_minor[seqL[i]] != minor)
			return (SMC_FAILURE);
	}

	LOCK_DATA(list);

	if (ctsmc_enQ(&list->smseq_queue, count, seqL) != B_TRUE) {
		UNLOCK_DATA(list);
		return (SMC_FAILURE);
	}

	/*
	 * Clear mask for each sequence number.
	 */
	for (i = 0; i < count; i++)
		list->smseq_mask &= ~((uint64_t)1 << seqL[i]);

	UNLOCK_DATA(list);

	return (SMC_SUCCESS);
}

/*
 * Given a minor number, remove all it's seq#
 * allocations; This routine will be called
 * when close is called, so that all the
 * dependencies for this minor number are freed
 */
static void
ctsmc_removeIPMSeqMinor(ctsmc_ipmi_seq_t *list, uint8_t minor)
{
	uint8_t i;
	ctsmc_queue_t *queue = &list->smseq_queue;
	uint64_t mask = list->smseq_mask;

	/*
	 * For every sequence number allocated, check whether
	 * they're allocated by this minor node, and if so, free
	 * them
	 */
	LOCK_DATA(list);
	/*
	 * Starting from end, for each entry, check whether this minor
	 * number owns this sequence
	 */
	for (i = 0; mask; mask >>= 1, i++) {
		if ((mask&1) && list->smseq_minor[i] == minor) {
			list->smseq_mask &= ~((uint64_t)1 << i);
			(void) ctsmc_enQ(queue, 1, &i);
		}
	}

	UNLOCK_DATA(list);
}

/*
 * Return list corresponding to a given destination address
 */
static ctsmc_dest_seq_entry_t *
ctsmc_searchIPMList(ctsmc_state_t *ctsmc, uint8_t dAddr)
{
	ctsmc_dest_seq_entry_t *entry;

	if (ctsmc->ctsmc_ipmb_seq == NULL)
		return (NULL);

	entry = ctsmc->ctsmc_ipmb_seq->head;

	while (entry != NULL) {
		if (entry->ctsmc_ipmi_seq_dAddr == dAddr)
			break;

		entry = entry->next;
	}

	return (entry);
}

/*
 * Given a sequence number, find which minor number owns it
 * for a particular destination
 */
int
ctsmc_findSeqOwner(ctsmc_state_t *ctsmc, uint8_t dAddr, uint8_t seq,
		uint8_t *minor)
{
	ctsmc_dest_seq_entry_t *entry;
	ctsmc_ipmi_seq_t *list;
	entry = ctsmc_searchIPMList(ctsmc, dAddr);

	if (seq >= SMC_IMAX_SEQ || entry == NULL)
		return (SMC_FAILURE);

	/*
	 * Check if this sequence number is allocated and if so
	 * who is the rightful owner
	 */
	list = &entry->ctsmc_ipmi_seq_seqL;
	if (list->smseq_mask & ((uint64_t)1 << seq)) {
		*minor = list->smseq_minor[seq];
		return (SMC_SUCCESS);
	}

	return (SMC_FAILURE);
}

/*
 * ================================================================
 * Routines to allocate seq# to a minor, given a destination address
 * ================================================================
 */

/*
 * Initialize per destination seq# list
 */
void
ctsmc_initIPMSeqList(ctsmc_state_t *ctsmc)
{
	/*
	 * make sure list is uninitialized
	 */
	ASSERT(ctsmc->ctsmc_ipmb_seq == NULL);

	ctsmc->ctsmc_ipmb_seq = NEW(1, ctsmc_dest_seq_list_t);
	mutex_init(&ctsmc->ctsmc_ipmb_seq->lock, NULL, MUTEX_DRIVER, NULL);
	ctsmc->ctsmc_ipmb_seq->head = NULL;
}

/*
 * Frees entire per destination seq# list
 */
void
ctsmc_freeIPMSeqList(ctsmc_state_t *ctsmc)
{
	ctsmc_dest_seq_entry_t *list, *entry;

	/*
	 * make sure list is not NULL
	 */
	ASSERT(ctsmc->ctsmc_ipmb_seq != NULL);

	/*
	 * Traverse the list and free sequence number list for each
	 * destination
	 */
	LOCK_DATA(ctsmc->ctsmc_ipmb_seq);

	list = ctsmc->ctsmc_ipmb_seq->head;
	while (list != NULL) {
		entry = list;
		list = NEXT(entry);
		ctsmc_freeIPMSeqArr(&entry->ctsmc_ipmi_seq_seqL);
		FREE(entry, 1, ctsmc_dest_seq_entry_t);
	}

	UNLOCK_DATA(ctsmc->ctsmc_ipmb_seq);

	mutex_destroy(&ctsmc->ctsmc_ipmb_seq->lock);
	FREE(ctsmc->ctsmc_ipmb_seq, 1, ctsmc_dest_seq_list_t);
	ctsmc->ctsmc_ipmb_seq = NULL;
}

/*
 * Routine to allocate a block of sequence numbers for
 * a given destination to a minor number
 */
static int
ctsmc_allocIPMSeqBlock(ctsmc_minor_t *mnode_p, uint8_t dAddr,
		int8_t count, uint8_t *seqL)
{
	ctsmc_state_t *ctsmc = mnode_p->ctsmc_state;
	uint8_t minor = mnode_p->minor;
	ctsmc_dest_seq_entry_t *entry;
	/*
	 * Check whether list for this destination address is
	 * already allocated
	 */
	entry = ctsmc_searchIPMList(ctsmc, dAddr);

	/*
	 * If not already allocated, allocate one and attach
	 * to head of list. This entry is only freed when driver
	 * is unloaded.
	 */
	if (entry == NULL) {
		entry = NEW(1, ctsmc_dest_seq_entry_t);
		LOCK_DATA(ctsmc->ctsmc_ipmb_seq);

		ctsmc_initIPMSeqArr(&entry->ctsmc_ipmi_seq_seqL);
		entry->ctsmc_ipmi_seq_dAddr = dAddr;
		entry->next = ctsmc->ctsmc_ipmb_seq->head;
		ctsmc->ctsmc_ipmb_seq->head = entry;

		UNLOCK_DATA(ctsmc->ctsmc_ipmb_seq);
	}

	/*
	 * Now allocate requested sequence numbers
	 */
	return (ctsmc_reserveSeq(&entry->ctsmc_ipmi_seq_seqL,
			minor, count, seqL));
}

/*
 * Routines to Free seq#s allocated to a minor, given
 * destination address
 */
static int
ctsmc_freeIPMSeqBlock(ctsmc_minor_t *mnode_p, uint8_t dAddr,
		int8_t count, uint8_t *seqL)
{
	ctsmc_state_t *ctsmc = mnode_p->ctsmc_state;
	uint8_t minor = mnode_p->minor;
	ctsmc_dest_seq_entry_t *entry;

	/*
	 * Check whether list for this sequence number is
	 * already allocated
	 */
	entry = ctsmc_searchIPMList(ctsmc, dAddr);

	if (entry == NULL) {
		if (count == -1)
			return (SMC_SUCCESS);
		else
			return (SMC_FAILURE);
	}

	if (count == -1) {
		ctsmc_removeIPMSeqMinor(&entry->ctsmc_ipmi_seq_seqL, minor);
		return (SMC_SUCCESS);
	}

	/*
	 * Freeing the list of specified sequence numbers
	 */
	return (ctsmc_unreserveIPMSeq(&entry->ctsmc_ipmi_seq_seqL,
				minor, count, seqL));
}

/*
 * Given a minor number, free all sequence numbers reserved by it
 * for all destinations currently allocated
 */
void
ctsmc_freeAllIPMSeqMinor(ctsmc_state_t *ctsmc, uint8_t minor)
{
	ctsmc_dest_seq_entry_t *list = ctsmc->ctsmc_ipmb_seq->head;

	while (list != NULL) {
		ctsmc_removeIPMSeqMinor(&list->ctsmc_ipmi_seq_seqL, minor);

		list = NEXT(list);
	}
}

/*
 * Validate the seqdesc specified as part of RESERVE_SEQ/FREE_SEQ
 * ioctls. We pass the length passed from ioctl and the sc_seqdesc_t *
 * We use it for both reserve and free, and no need to check for
 * duplicate during reserve, returns SMC_SUCCESS is no error, else
 * SMC_FAILURE.
 */
static int
ctsmc_validate_seqspec(int ioclen, sc_seqdesc_t *seqdesc, int op)
{
	uint8_t misclen = sizeof (sc_seqdesc_t) - SC_SEQ_SZ;
	int8_t arglen = seqdesc->n_seqn;

	/*
	 * If freeing all sequence numners for a given destination
	 * address on current stream, return success
	 */
	if ((op == SCIOC_FREE_SEQN) && (arglen == -1))
		return (SMC_SUCCESS);

	/*
	 * Request to reserve or free certain number of IPMI sequences.
	 * Make sure the number of commands specified does not exceed
	 * maximum, and also length passed in ioctl is enough to
	 * contain these commands
	 */
	if ((arglen < 1) || (arglen > SC_SEQ_SZ) ||
			(ioclen < arglen + misclen))
		return (SMC_FAILURE);

	/*
	 * Now make sure each of these commands are valid and
	 * there are no duplicates
	 */
	if ((op == SCIOC_FREE_SEQN) &&
		(ctsmc_detect_duplicate(arglen, seqdesc->seq_numbers, NULL) !=
			SMC_SUCCESS))
		return (SMC_FAILURE);

	return (SMC_SUCCESS);
}

typedef int (*ctsmc_seqFn_t)(ctsmc_minor_t *, uint8_t, int8_t, uint8_t *);

int
ctsmc_update_seq_list(ctsmc_minor_t *mnode_p, int ioc_cmd,
		sc_seqdesc_t *seqdesc, int ioclen)
{

	ctsmc_seqFn_t seqFn;
	int8_t n_seqn;
	uint8_t dAddr = seqdesc->d_addr;

	if (ctsmc_validate_seqspec(ioclen, seqdesc,
			ioc_cmd) != SMC_SUCCESS)
		return (SMC_FAILURE);

	n_seqn = (int8_t)seqdesc->n_seqn;

	seqFn = (ioc_cmd == SCIOC_RESERVE_SEQN) ?
		ctsmc_allocIPMSeqBlock : ctsmc_freeIPMSeqBlock;

	/*
	 * Now request for sequence numbers
	 */
	if ((*seqFn)(mnode_p, dAddr, n_seqn,
				seqdesc->seq_numbers) != SMC_SUCCESS)
		return (SMC_FAILURE);
	else
		return (SMC_SUCCESS);
}

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
 * ctsmc_buf - Buffer list management routines
 */

#include <sys/types.h>
#include <sys/dditypes.h>
#include <sys/sunddi.h>
#include <sys/ksynch.h>

#include <sys/ctsmc.h>

void
ctsmc_initBufList(ctsmc_state_t *ctsmc)
{
	int i = 0;
	ctsmc_hwinfo_t *smchw = ctsmc->ctsmc_hw;
	ctsmc_buflist_t *list = ctsmc->ctsmc_buf_list
		= NEW(1, ctsmc_buflist_t);

	list->last_buf = 0;

	/*
	 * initialize queue with buffer ids 1...(SMC_NUM_BUFS - 1)
	 */
	ctsmc_initQ(&list->queue, SMC_NUM_BUFS - 1,
			SMC_NUM_BUFS - 1, 1, 1);
	mutex_init(&list->lock, NULL, MUTEX_DRIVER, (void *)smchw->smh_ib);

	for (i = 0; i < SMC_NUM_BUFS/SMC_BLOCK_SZ; i++)
		list->mask[i] = 0;
	for (i = 0; i < SMC_NUM_BUFS; i++) {
		list->minor[i] = 0;
		list->rspbuf[i] = NULL;
	}

	/*
	 * Pre-allocate entry for buffer 0
	 */
	list->rspbuf[0] = NEW(1, sc_rspmsg_t);
	SETBIT(list->mask[0], 0);
}

void
ctsmc_freeBufList(ctsmc_state_t *ctsmc)
{
	ctsmc_buflist_t *list = ctsmc->ctsmc_buf_list;

	mutex_destroy(&list->lock);
	ctsmc_freeQ(&list->queue);

	FREE(list->rspbuf[0], 1, sc_rspmsg_t);
	FREE(list, 1, ctsmc_buflist_t);
}

/*
 * Allocate a buffer where the response packet
 * is copied to
 */
uint8_t
ctsmc_allocBuf(ctsmc_state_t *ctsmc, uint8_t minor, uint8_t *index)
{
	ctsmc_buflist_t *list = ctsmc->ctsmc_buf_list;

	LOCK_DATA(list);

	/*
	 * Try to grab a free buffer. We can only get
	 * an index value from 1 ... (SMC_NUM_BUFS - 1)
	 */
	if (ctsmc_deQ(&list->queue, 1, index) != B_TRUE) {
		UNLOCK_DATA(list);
		return (SMC_FAILURE);
	}

	/*
	 * Before we allocate the buffer, make sure it's
	 * NULL, otherwise we are in an inconsistent state
	 */
	ASSERT(list->rspbuf[*index] == NULL);

	/*
	 * Allocate memory for buffer in this index
	 */
	list->rspbuf[*index] = NEW(1, sc_rspmsg_t);
	list->minor[*index] = minor;
	SETBIT(list->mask[NUM_TO_BLOCK(*index)], NUM_OFFSET(*index));
	UNLOCK_DATA(list);

	return (SMC_SUCCESS);
}

void
ctsmc_freeBuf(ctsmc_state_t *ctsmc, uint8_t index)
{
	ctsmc_buflist_t *list = ctsmc->ctsmc_buf_list;

	/*
	 * Make sure it's a valid index, and we are
	 * not trying to free something which is
	 * already free
	 */
	if ((index == 0) || (list->rspbuf[index] == NULL))
		return;

	LOCK_DATA(list);
	/*
	 * Make sure we are trying to do the right thing
	 */
	if (ctsmc_enQ(&list->queue, 1, &index) != B_TRUE) {
		UNLOCK_DATA(list);
		return;
	}

	/*
	 * De-allocate memory allocated for this buffer
	 */
	if (list->rspbuf[index] != NULL)
		FREE(list->rspbuf[index], 1, sc_rspmsg_t);
	list->rspbuf[index] = NULL;
	list->minor[index] = 0;
	CLRBIT(list->mask[NUM_TO_BLOCK(index)], NUM_OFFSET(index));

	UNLOCK_DATA(list);
}

/*
 * Given a minor number, free all buffers allocated by it
 */
void
ctsmc_freeAllBufs(ctsmc_state_t *ctsmc, uint8_t minor)
{
	int i, j;
	ctsmc_buflist_t *list = ctsmc->ctsmc_buf_list;
	uint16_t mask, *b_mask = list->mask;

	for (i = 0; i < SMC_NUM_BUFS/SMC_BLOCK_SZ; i++) {
		mask = b_mask[i];
		for (j = 0; mask; mask >>= 1, j++) {
			if ((mask&1) && (minor ==
					list->minor[i * SMC_BLOCK_SZ + j])) {
				ctsmc_freeBuf(ctsmc, i * SMC_BLOCK_SZ + j);
			}
		}
	}
}

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
 * ctsmc_minor - minor number management for SMC driver
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

extern void ctsmc_clear_seq_refs(ctsmc_state_t *ctsmc, uint8_t minor);
extern void ctsmc_clear_cmdspec_refs_all(ctsmc_state_t *ctsmc,
		uint8_t minor);
extern void ctsmc_freeAllIPMSeqMinor(ctsmc_state_t *ctsmc, uint8_t minor);
extern void ctsmc_freeAllBufs(ctsmc_state_t *ctsmc, uint8_t minor);

/*
 * Initialize Minor number array
 */
void
ctsmc_init_minor_list(ctsmc_state_t *ctsmc)
{
	int i;
	ctsmc_minor_list_t *list = NEW(1, ctsmc_minor_list_t);
	ctsmc->ctsmc_minor_list = list;

	for (i = 0; i < MAX_SMC_MINORS; i++)
		list->minor_list[i] = NULL;

	ctsmc_initQ(&list->queue, MAX_SMC_MINORS, MAX_SMC_MINORS, 0, 1);
	mutex_init(&list->lock, NULL, MUTEX_DRIVER, NULL);

}

/*
 * Free minor number list, return error if a device instance
 * is open
 */
int
ctsmc_free_minor_list(ctsmc_state_t *ctsmc)
{
	int i;
	ctsmc_minor_list_t *list = ctsmc->ctsmc_minor_list;

	for (i = 0; i < MAX_SMC_MINORS; i++) {
		if (list->minor_list[i] != NULL)
			return (SMC_FAILURE);
	}

	mutex_destroy(&list->lock);

	ctsmc_freeQ(&list->queue);
	FREE(ctsmc->ctsmc_minor_list, 1, ctsmc_minor_list_t);

	return (SMC_SUCCESS);
}

/*
 * Allocate a new minor number on a clone open. pointer to the
 * ctsmc_minor_t structure is returned in mnode. This function
 * may be called by an internal client as well and may not have
 * any associated queue. Once a minor number is allocated, the
 * caller will need to fill up members of ctsmc_minor_t returned.
 */
int
ctsmc_alloc_minor(ctsmc_state_t *ctsmc, ctsmc_minor_t **mnode,
		uint8_t *minor)
{
	ctsmc_minor_list_t *list = ctsmc->ctsmc_minor_list;
	ctsmc_minor_t *mnode_p;	/* Head of a block */

	LOCK_DATA(list);

	if (ctsmc_deQ(&list->queue, 1, minor) != B_TRUE) {
		UNLOCK_DATA(list);
		return (SMC_FAILURE);
	}

	/*
	 * Now we have a minor number, allocate memory for this
	 * minor structure
	 */
	mnode_p = NEW(1, ctsmc_minor_t);
	*mnode = list->minor_list[*minor] = mnode_p;
	mutex_init(&mnode_p->lock, NULL, MUTEX_DRIVER, NULL);
	mnode_p->minor = *minor;
	mnode_p->ctsmc_state = ctsmc;

	mnode_p->req_events = mnode_p->ctsmc_flags = 0;

	UNLOCK_DATA(list);

	return (SMC_SUCCESS);
}

int
ctsmc_free_minor(ctsmc_state_t *ctsmc, uint8_t minor)
{
	ctsmc_minor_list_t *list = ctsmc->ctsmc_minor_list;
	ctsmc_minor_t *mnode = list->minor_list[minor];

	if (list->minor_list[minor] == NULL)
		return (SMC_FAILURE);

	LOCK_DATA(list);

	/*
	 * Return this minor number to queue
	 */
	if (ctsmc_enQ(&list->queue, 1, &minor) != B_TRUE) {
		UNLOCK_DATA(list);
		return (SMC_SUCCESS);
	}

	mutex_destroy(&mnode->lock);
	FREE(mnode, 1, ctsmc_minor_t);
	list->minor_list[minor] = NULL;

	UNLOCK_DATA(list);

	/*
	 * Remove all references of this minor
	 */
	ctsmc_clear_seq_refs(ctsmc, minor);
	ctsmc_clear_cmdspec_refs_all(ctsmc, minor);
	ctsmc_freeAllIPMSeqMinor(ctsmc, minor);
	ctsmc_freeAllBufs(ctsmc, minor);

	return (SMC_SUCCESS);
}

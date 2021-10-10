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
 *  Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * mcxnex_cq.c
 *    Mcxnex Completion Queue Processing Routines
 *
 *    Implements all the routines necessary for allocating, freeing, resizing,
 *    and handling the completion type events that the Mcxnex hardware can
 *    generate.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/bitmap.h>
#include <sys/sysmacros.h>

#include "mcxnex.h"

int mcxnex_should_panic = 0;	/* debugging aid */

#define	mcxnex_cq_update_ci_doorbell(cq)				\
	/* Build the doorbell record data (low 24 bits only) */		\
	MCXNEX_UAR_DB_RECORD_WRITE(cq->cq_arm_ci_vdbr,			\
	    cq->cq_consindx & 0x00FFFFFF)

static int mcxnex_cq_arm_doorbell(mcxnex_state_t *state, mcxnex_cqhdl_t cq,
    uint_t cmd);
#pragma inline(mcxnex_cq_arm_doorbell)
static void mcxnex_arm_cq_dbr_init(mcxnex_dbr_t *cq_arm_dbr);
#pragma inline(mcxnex_arm_cq_dbr_init)
static void mcxnex_cq_cqe_consume(mcxnex_state_t *state, mcxnex_cqhdl_t cq,
    mcxnex_hw_cqe_t *cqe, ibt_wc_t *wc);
static void mcxnex_cq_errcqe_consume(mcxnex_state_t *state, mcxnex_cqhdl_t cq,
    mcxnex_hw_cqe_t *cqe, ibt_wc_t *wc);
static void mcxnex_cqe_sync(mcxnex_cqhdl_t cq, mcxnex_hw_cqe_t *cqe,
    uint_t flag);


/*
 * mcxnex_cq_alloc()
 *    Context: Can be called only from user or kernel context.
 */
int
mcxnex_cq_alloc(mcxnex_state_t *state, ibt_cq_hdl_t ibt_cqhdl,
    ibt_cq_attr_t *cq_attr, uint_t *actual_size, mcxnex_cqhdl_t *cqhdl,
    uint_t sleepflag)
{
	mcxnex_rsrc_t		*cqc, *rsrc;
	mcxnex_umap_db_entry_t	*umapdb;
	mcxnex_hw_cqc_t		cqc_entry;
	mcxnex_cqhdl_t		cq;
	ibt_mr_attr_t		mr_attr;
	mcxnex_mr_options_t	op;
	mcxnex_pdhdl_t		pd;
	mcxnex_mrhdl_t		mr;
	mcxnex_hw_cqe_t		*buf;
	uint64_t		value;
	uint32_t		log_cq_size, uarpg;
	uint_t			cq_is_umap;
	int			status, flag;

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*cq_attr))

	/*
	 * Determine whether CQ is being allocated for userland access or
	 * whether it is being allocated for kernel access.  If the CQ is
	 * being allocated for userland access, then lookup the UAR
	 * page number for the current process.  Note:  If this is not found
	 * (e.g. if the process has not previously open()'d the Mcxnex driver),
	 * then an error is returned.
	 */
	cq_is_umap = (cq_attr->cq_flags & IBT_CQ_USER_MAP) ? 1 : 0;
	if (cq_is_umap) {
		status = mcxnex_umap_db_find(state->hs_instance, ddi_get_pid(),
		    MLNX_UMAP_UARPG_RSRC, &value, 0, NULL);
		if (status != DDI_SUCCESS) {
			status = IBT_INVALID_PARAM;
			goto cqalloc_fail;
		}
		uarpg = ((mcxnex_rsrc_t *)(uintptr_t)value)->hr_indx;
	} else {
		uarpg = state->hs_kernel_uar_index;
	}

	/* Use the internal protection domain (PD) for setting up CQs */
	pd = state->hs_pdhdl_internal;

	/* Increment the reference count on the protection domain (PD) */
	mcxnex_pd_refcnt_inc(pd);

	/*
	 * Allocate an CQ context entry.  This will be filled in with all
	 * the necessary parameters to define the Completion Queue.  And then
	 * ownership will be passed to the hardware in the final step
	 * below.  If we fail here, we must undo the protection domain
	 * reference count.
	 */
	status = mcxnex_rsrc_alloc(state, MCXNEX_CQC, 1, sleepflag, &cqc);
	if (status != DDI_SUCCESS) {
		status = IBT_INSUFF_RESOURCE;
		goto cqalloc_fail1;
	}

	/*
	 * Allocate the software structure for tracking the completion queue
	 * (i.e. the Mcxnex Completion Queue handle).  If we fail here, we must
	 * undo the protection domain reference count and the previous
	 * resource allocation.
	 */
	status = mcxnex_rsrc_alloc(state, MCXNEX_CQHDL, 1, sleepflag, &rsrc);
	if (status != DDI_SUCCESS) {
		status = IBT_INSUFF_RESOURCE;
		goto cqalloc_fail2;
	}
	cq = (mcxnex_cqhdl_t)rsrc->hr_addr;
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*cq))
	cq->cq_is_umap = cq_is_umap;
	cq->cq_cqnum = cqc->hr_indx;	/* just use index, implicit in Mcxnex */

	/*
	 * If this will be a user-mappable CQ, then allocate an entry for
	 * the "userland resources database".  This will later be added to
	 * the database (after all further CQ operations are successful).
	 * If we fail here, we must undo the reference counts and the
	 * previous resource allocation.
	 */
	if (cq->cq_is_umap) {
		umapdb = mcxnex_umap_db_alloc(state->hs_instance, cq->cq_cqnum,
		    MLNX_UMAP_CQMEM_RSRC, (uint64_t)(uintptr_t)rsrc);
		if (umapdb == NULL) {
			status = IBT_INSUFF_RESOURCE;
			goto cqalloc_fail3;
		}
	}


	/*
	 * Allocate the doorbell record.  We'll need one for the CQ, handling
	 * both consumer index (SET CI) and the CQ state (CQ ARM).
	 */

	status = mcxnex_dbr_alloc(state, uarpg, &cq->cq_arm_ci_dbr_acchdl,
	    &cq->cq_arm_ci_vdbr, &cq->cq_arm_ci_pdbr, &cq->cq_dbr_mapoffset);
	if (status != DDI_SUCCESS) {
		status = IBT_INSUFF_RESOURCE;
		goto cqalloc_fail4;
	}

	/*
	 * Calculate the appropriate size for the completion queue.
	 * Note:  All Mcxnex CQs must be a power-of-2 minus 1 in size.  Also
	 * they may not be any smaller than MCXNEX_CQ_MIN_SIZE.  This step is
	 * to round the requested size up to the next highest power-of-2
	 */
	cq_attr->cq_size = max(cq_attr->cq_size, MCXNEX_CQ_MIN_SIZE);
	log_cq_size = highbit(cq_attr->cq_size);

	/*
	 * Next we verify that the rounded-up size is valid (i.e. consistent
	 * with the device limits and/or software-configured limits)
	 */
	if (log_cq_size > state->hs_cfg_profile->cp_log_max_cq_sz) {
		status = IBT_HCA_CQ_EXCEEDED;
		goto cqalloc_fail4a;
	}

	/*
	 * Allocate the memory for Completion Queue.
	 *
	 * Note: Although we use the common queue allocation routine, we
	 * always specify MCXNEX_QUEUE_LOCATION_NORMAL (i.e. CQ located in
	 * kernel system memory) for kernel CQs because it would be
	 * inefficient to have CQs located in DDR memory.  This is primarily
	 * because CQs are read from (by software) more than they are written
	 * to. (We always specify MCXNEX_QUEUE_LOCATION_USERLAND for all
	 * user-mappable CQs for a similar reason.)
	 * It is also worth noting that, unlike Mcxnex QP work queues,
	 * completion queues do not have the same strict alignment
	 * requirements.  It is sufficient for the CQ memory to be both
	 * aligned to and bound to addresses which are a multiple of CQE size.
	 */
	cq->cq_cqinfo.qa_size = (1 << log_cq_size) * sizeof (mcxnex_hw_cqe_t);

	cq->cq_cqinfo.qa_alloc_align = PAGESIZE;
	cq->cq_cqinfo.qa_bind_align  = PAGESIZE;
	if (cq->cq_is_umap) {
		cq->cq_cqinfo.qa_location = MCXNEX_QUEUE_LOCATION_USERLAND;
	} else {
		cq->cq_cqinfo.qa_location = MCXNEX_QUEUE_LOCATION_NORMAL;
		mcxnex_arm_cq_dbr_init(cq->cq_arm_ci_vdbr);
	}
	status = mcxnex_queue_alloc(state, &cq->cq_cqinfo, sleepflag);
	if (status != DDI_SUCCESS) {
		status = IBT_INSUFF_RESOURCE;
		goto cqalloc_fail4;
	}
	buf = (mcxnex_hw_cqe_t *)cq->cq_cqinfo.qa_buf_aligned;
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*buf))

	/*
	 * The ownership bit of the CQE's is set by the HW during the process
	 * of transferrring ownership of the CQ (PRM 09.35c, 14.2.1, note D1
	 *
	 */

	/*
	 * Register the memory for the CQ.  The memory for the CQ must
	 * be registered in the Mcxnex TPT tables.  This gives us the LKey
	 * to specify in the CQ context below.  Note: If this is a user-
	 * mappable CQ, then we will force DDI_DMA_CONSISTENT mapping.
	 */
	flag = (sleepflag == DDI_SLEEP) ?  IBT_MR_SLEEP : IBT_MR_NOSLEEP;
	mr_attr.mr_vaddr = (uint64_t)(uintptr_t)buf;
	mr_attr.mr_len	 = cq->cq_cqinfo.qa_size;
	mr_attr.mr_as	 = NULL;
	mr_attr.mr_flags = flag | IBT_MR_ENABLE_LOCAL_WRITE;
	op.mro_bind_type   = state->hs_cfg_profile->cp_iommu_bypass;
	op.mro_bind_dmahdl = cq->cq_cqinfo.qa_dmahdl;
	op.mro_bind_override_addr = 0;
	status = mcxnex_mr_register(state, pd, &mr_attr, &mr, &op,
	    MCXNEX_CQ_CMPT);
	if (status != DDI_SUCCESS) {
		status = IBT_INSUFF_RESOURCE;
		goto cqalloc_fail5;
	}
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*mr))

	/* Sync entire CQ for use by the hardware. */
	(void) ddi_dma_sync(mr->mr_bindinfo.bi_dmahdl, 0,
	    cq->cq_cqinfo.qa_size, DDI_DMA_SYNC_FORDEV);

	/*
	 * Fill in the CQC entry.  This is the final step before passing
	 * ownership of the CQC entry to the Mcxnex hardware.  We use all of
	 * the information collected/calculated above to fill in the
	 * requisite portions of the CQC.  Note: If this CQ is going to be
	 * used for userland access, then we need to set the UAR page number
	 * appropriately (otherwise it's a "don't care")
	 */
	bzero(&cqc_entry, sizeof (mcxnex_hw_cqc_t));

	cq->cq_eqnum		= MCXNEX_CQ_EQNUM_GET(state);
	cq->cq_erreqnum		= MCXNEX_CQ_ERREQNUM_GET(state);

	cqc_entry.state		= MCXNEX_CQ_DISARMED;
	cqc_entry.pg_offs	= cq->cq_cqinfo.qa_pgoffs >> 5;
	cqc_entry.log_cq_sz	= log_cq_size;
	cqc_entry.usr_page	= uarpg;
	cqc_entry.c_eqn		= cq->cq_eqnum;
	cqc_entry.log2_pgsz	= mr->mr_log2_pgsz;
	cqc_entry.mtt_base_addh = (uint32_t)((mr->mr_mttaddr >> 32) & 0xFF);
	cqc_entry.mtt_base_addl = mr->mr_mttaddr >> 3;
	cqc_entry.dbr_addrh = (uint32_t)((uint64_t)cq->cq_arm_ci_pdbr >> 32);
	cqc_entry.dbr_addrl = (uint32_t)((uint64_t)cq->cq_arm_ci_pdbr >> 3);

	/*
	 * Write the CQC entry to hardware - we pass ownership of
	 * the entry to the hardware (using the Mcxnex SW2HW_CQ firmware
	 * command).  Note: In general, this operation shouldn't fail.  But
	 * if it does, we have to undo everything we've done above before
	 * returning error.
	 */
	status = mcxnex_cmn_ownership_cmd_post(state, SW2HW_CQ, &cqc_entry,
	    sizeof (mcxnex_hw_cqc_t), cq->cq_cqnum, sleepflag);
	if (status != MCXNEX_CMD_SUCCESS) {
		cmn_err(CE_CONT, "Mcxnex: SW2HW_CQ command failed: %08x\n",
		    status);
		if (status == MCXNEX_CMD_INVALID_STATUS) {
			mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_SRV_LOST);
		}
		status = DDI_FAILURE;
		goto cqalloc_fail6;
	}

	/*
	 * Fill in the rest of the Mcxnex Completion Queue handle.  Having
	 * successfully transferred ownership of the CQC, we can update the
	 * following fields for use in further operations on the CQ.
	 */
	cq->cq_resize_hdl = 0;
	cq->cq_cqcrsrcp	  = cqc;
	cq->cq_rsrcp	  = rsrc;
	cq->cq_consindx	  = 0;
		/* least restrictive */
	cq->cq_buf	  = buf;
	cq->cq_bufsz	  = (1 << log_cq_size);
	cq->cq_log_cqsz	  = log_cq_size;
	cq->cq_mrhdl	  = mr;
	cq->cq_refcnt	  = 0;
	cq->cq_is_special = 0;
	cq->cq_uarpg	  = uarpg;
	cq->cq_umap_dhp	  = (devmap_cookie_t)NULL;
	avl_create(&cq->cq_wrid_wqhdr_avl_tree, mcxnex_wrid_workq_compare,
	    sizeof (struct mcxnex_workq_avl_s),
	    offsetof(struct mcxnex_workq_avl_s, wqa_link));

	cq->cq_hdlrarg	  = (void *)ibt_cqhdl;

	/*
	 * Put CQ handle in Mcxnex CQNum-to-CQHdl list.  Then fill in the
	 * "actual_size" and "cqhdl" and return success
	 */
	ASSERT(state->hs_cqhdl[cqc->hr_indx] == NULL);
	state->hs_cqhdl[cqc->hr_indx] = cq;

	/*
	 * If this is a user-mappable CQ, then we need to insert the previously
	 * allocated entry into the "userland resources database".  This will
	 * allow for later lookup during devmap() (i.e. mmap()) calls.
	 */
	if (cq->cq_is_umap) {
		mcxnex_umap_db_add(umapdb);
	}

	/*
	 * Fill in the return arguments (if necessary).  This includes the
	 * real completion queue size.
	 */
	if (actual_size != NULL) {
		*actual_size = (1 << log_cq_size) - 1;
	}
	*cqhdl = cq;

	return (DDI_SUCCESS);

/*
 * The following is cleanup for all possible failure cases in this routine
 */
cqalloc_fail6:
	if (mcxnex_mr_deregister(state, &mr, MCXNEX_MR_DEREG_ALL,
	    sleepflag) != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "failed to deregister CQ memory");
	}
cqalloc_fail5:
	mcxnex_queue_free(&cq->cq_cqinfo);
cqalloc_fail4a:
	mcxnex_dbr_free(state, uarpg, cq->cq_arm_ci_vdbr);
cqalloc_fail4:
	if (cq_is_umap) {
		mcxnex_umap_db_free(umapdb);
	}
cqalloc_fail3:
	mcxnex_rsrc_free(state, &rsrc);
cqalloc_fail2:
	mcxnex_rsrc_free(state, &cqc);
cqalloc_fail1:
	mcxnex_pd_refcnt_dec(pd);
cqalloc_fail:
	return (status);
}


/*
 * mcxnex_cq_free()
 *    Context: Can be called only from user or kernel context.
 */
/* ARGSUSED */
int
mcxnex_cq_free(mcxnex_state_t *state, mcxnex_cqhdl_t *cqhdl, uint_t sleepflag)
{
	mcxnex_rsrc_t		*cqc, *rsrc;
	mcxnex_umap_db_entry_t	*umapdb;
	mcxnex_hw_cqc_t		cqc_entry;
	mcxnex_pdhdl_t		pd;
	mcxnex_mrhdl_t		mr;
	mcxnex_cqhdl_t		cq, resize;
	uint32_t		cqnum;
	uint64_t		value;
	uint_t			maxprot;
	int			status;

	/*
	 * Pull all the necessary information from the Mcxnex Completion Queue
	 * handle.  This is necessary here because the resource for the
	 * CQ handle is going to be freed up as part of this operation.
	 */
	cq	= *cqhdl;
	mutex_enter(&cq->cq_lock);
	cqc	= cq->cq_cqcrsrcp;
	rsrc	= cq->cq_rsrcp;
	pd	= state->hs_pdhdl_internal;
	mr	= cq->cq_mrhdl;
	cqnum	= cq->cq_cqnum;

	resize = cq->cq_resize_hdl;		/* save the handle for later */

	/*
	 * If there are work queues still associated with the CQ, then return
	 * an error.  Otherwise, we will be holding the CQ lock.
	 */
	if (cq->cq_refcnt != 0) {
		mutex_exit(&cq->cq_lock);
		return (IBT_CQ_BUSY);
	}

	/*
	 * If this was a user-mappable CQ, then we need to remove its entry
	 * from the "userland resources database".  If it is also currently
	 * mmap()'d out to a user process, then we need to call
	 * devmap_devmem_remap() to remap the CQ memory to an invalid mapping.
	 * We also need to invalidate the CQ tracking information for the
	 * user mapping.
	 */
	if (cq->cq_is_umap) {
		status = mcxnex_umap_db_find(state->hs_instance, cqnum,
		    MLNX_UMAP_CQMEM_RSRC, &value, MCXNEX_UMAP_DB_REMOVE,
		    &umapdb);
		if (status != DDI_SUCCESS) {
			mutex_exit(&cq->cq_lock);
			MCXNEX_WARNING(state, "failed to find in database");
			return (DDI_FAILURE);
		}
		mcxnex_umap_db_free(umapdb);
		if (cq->cq_umap_dhp != NULL) {
			maxprot = (PROT_READ | PROT_WRITE | PROT_USER);
			status = devmap_devmem_remap(cq->cq_umap_dhp,
			    state->hs_dip, 0, 0, cq->cq_cqinfo.qa_size,
			    maxprot, DEVMAP_MAPPING_INVALID, NULL);
			if (status != DDI_SUCCESS) {
				mutex_exit(&cq->cq_lock);
				MCXNEX_WARNING(state, "failed in CQ memory "
				    "devmap_devmem_remap()");
				return (DDI_FAILURE);
			}
			cq->cq_umap_dhp = (devmap_cookie_t)NULL;
		}
	}

	/*
	 * Put NULL into the Arbel CQNum-to-CQHdl list.  This will allow any
	 * in-progress events to detect that the CQ corresponding to this
	 * number has been freed.
	 */
	state->hs_cqhdl[cqc->hr_indx] = NULL;

	mutex_exit(&cq->cq_lock);
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*cq))

	/*
	 * Reclaim CQC entry from hardware (using the Mcxnex HW2SW_CQ
	 * firmware command).  If the ownership transfer fails for any reason,
	 * then it is an indication that something (either in HW or SW) has
	 * gone seriously wrong.
	 */
	status = mcxnex_cmn_ownership_cmd_post(state, HW2SW_CQ, &cqc_entry,
	    sizeof (mcxnex_hw_cqc_t), cqnum, sleepflag);
	if (status != MCXNEX_CMD_SUCCESS) {
		MCXNEX_WARNING(state, "failed to reclaim CQC ownership");
		cmn_err(CE_CONT, "Mcxnex: HW2SW_CQ command failed: %08x\n",
		    status);
		if (status == MCXNEX_CMD_INVALID_STATUS) {
			mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_SRV_LOST);
		}
		return (DDI_FAILURE);
	}

	/*
	 * From here on, we start reliquishing resources - but check to see
	 * if a resize was in progress - if so, we need to relinquish those
	 * resources as well
	 */


	/*
	 * Deregister the memory for the Completion Queue.  If this fails
	 * for any reason, then it is an indication that something (either
	 * in HW or SW) has gone seriously wrong.  So we print a warning
	 * message and return.
	 */
	status = mcxnex_mr_deregister(state, &mr, MCXNEX_MR_DEREG_ALL,
	    sleepflag);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "failed to deregister CQ memory");
		return (DDI_FAILURE);
	}

	if (resize)	{	/* there was a pointer to a handle */
		mr = resize->cq_mrhdl;	/* reuse the pointer to the region */
		status = mcxnex_mr_deregister(state, &mr, MCXNEX_MR_DEREG_ALL,
		    sleepflag);
		if (status != DDI_SUCCESS) {
			MCXNEX_WARNING(state, "failed to deregister resize CQ "
			    "memory");
			return (DDI_FAILURE);
		}
	}

	/* Free the memory for the CQ */
	mcxnex_queue_free(&cq->cq_cqinfo);
	if (resize)	{
		mcxnex_queue_free(&resize->cq_cqinfo);
		/* and the temporary handle */
		kmem_free(resize, sizeof (struct mcxnex_sw_cq_s));
	}

	/* everything else does not matter for the resize in progress */

	/* Free the dbr */
	mcxnex_dbr_free(state, cq->cq_uarpg, cq->cq_arm_ci_vdbr);

	/* Free the Mcxnex Completion Queue handle */
	mcxnex_rsrc_free(state, &rsrc);

	/* Free up the CQC entry resource */
	mcxnex_rsrc_free(state, &cqc);

	/* Decrement the reference count on the protection domain (PD) */
	mcxnex_pd_refcnt_dec(pd);

	/* Set the cqhdl pointer to NULL and return success */
	*cqhdl = NULL;

	return (DDI_SUCCESS);
}


/*
 * mcxnex_cq_resize()
 *    Context: Can be called only from user or kernel context.
 */
int
mcxnex_cq_resize(mcxnex_state_t *state, mcxnex_cqhdl_t cq, uint_t req_size,
    uint_t *actual_size, uint_t sleepflag)
{
	mcxnex_hw_cqc_t		cqc_entry;
	mcxnex_cqhdl_t		resize_hdl;
	mcxnex_qalloc_info_t	new_cqinfo;
	ibt_mr_attr_t		mr_attr;
	mcxnex_mr_options_t	op;
	mcxnex_pdhdl_t		pd;
	mcxnex_mrhdl_t		mr;
	mcxnex_hw_cqe_t		*buf;
	uint32_t		new_prod_indx;
	uint_t			log_cq_size;
	int			status, flag;

	if (cq->cq_resize_hdl != 0) {	/* already in process */
		status = IBT_CQ_BUSY;
		goto cqresize_fail;
	}


	/* Use the internal protection domain (PD) for CQs */
	pd = state->hs_pdhdl_internal;

	/*
	 * Calculate the appropriate size for the new resized completion queue.
	 * Note:  All Mcxnex CQs must be a power-of-2 minus 1 in size.  Also
	 * they may not be any smaller than MCXNEX_CQ_MIN_SIZE.  This step is
	 * to round the requested size up to the next highest power-of-2
	 */
	req_size = max(req_size, MCXNEX_CQ_MIN_SIZE);
	log_cq_size = highbit(req_size);

	/*
	 * Next we verify that the rounded-up size is valid (i.e. consistent
	 * with the device limits and/or software-configured limits)
	 */
	if (log_cq_size > state->hs_cfg_profile->cp_log_max_cq_sz) {
		status = IBT_HCA_CQ_EXCEEDED;
		goto cqresize_fail;
	}

	/*
	 * Allocate the memory for newly resized Completion Queue.
	 *
	 * Note: Although we use the common queue allocation routine, we
	 * always specify MCXNEX_QUEUE_LOCATION_NORMAL (i.e. CQ located in
	 * kernel system memory) for kernel CQs because it would be
	 * inefficient to have CQs located in DDR memory.  This is the same
	 * as we do when we first allocate completion queues primarily
	 * because CQs are read from (by software) more than they are written
	 * to. (We always specify MCXNEX_QUEUE_LOCATION_USERLAND for all
	 * user-mappable CQs for a similar reason.)
	 * It is also worth noting that, unlike Mcxnex QP work queues,
	 * completion queues do not have the same strict alignment
	 * requirements.  It is sufficient for the CQ memory to be both
	 * aligned to and bound to addresses which are a multiple of CQE size.
	 */

	/* first, alloc the resize_handle */
	resize_hdl = kmem_zalloc(sizeof (struct mcxnex_sw_cq_s), KM_SLEEP);

	new_cqinfo.qa_size = (1 << log_cq_size) * sizeof (mcxnex_hw_cqe_t);
	new_cqinfo.qa_alloc_align = PAGESIZE;
	new_cqinfo.qa_bind_align  = PAGESIZE;
	if (cq->cq_is_umap) {
		new_cqinfo.qa_location = MCXNEX_QUEUE_LOCATION_USERLAND;
	} else {
		new_cqinfo.qa_location = MCXNEX_QUEUE_LOCATION_NORMAL;
	}
	status = mcxnex_queue_alloc(state, &new_cqinfo, sleepflag);
	if (status != DDI_SUCCESS) {
		/* free the resize handle */
		kmem_free(resize_hdl, sizeof (struct mcxnex_sw_cq_s));
		status = IBT_INSUFF_RESOURCE;
		goto cqresize_fail;
	}
	buf = (mcxnex_hw_cqe_t *)new_cqinfo.qa_buf_aligned;
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*buf))

	/*
	 * No initialization of the cq is needed - the command will do it
	 */

	/*
	 * Register the memory for the CQ.  The memory for the CQ must
	 * be registered in the Mcxnex TPT tables.  This gives us the LKey
	 * to specify in the CQ context below.
	 */
	flag = (sleepflag == DDI_SLEEP) ? IBT_MR_SLEEP : IBT_MR_NOSLEEP;
	mr_attr.mr_vaddr = (uint64_t)(uintptr_t)buf;
	mr_attr.mr_len	 = new_cqinfo.qa_size;
	mr_attr.mr_as	 = NULL;
	mr_attr.mr_flags = flag | IBT_MR_ENABLE_LOCAL_WRITE;
	op.mro_bind_type = state->hs_cfg_profile->cp_iommu_bypass;
	op.mro_bind_dmahdl = new_cqinfo.qa_dmahdl;
	op.mro_bind_override_addr = 0;
	status = mcxnex_mr_register(state, pd, &mr_attr, &mr, &op,
	    MCXNEX_CQ_CMPT);
	if (status != DDI_SUCCESS) {
		mcxnex_queue_free(&new_cqinfo);
		/* free the resize handle */
		kmem_free(resize_hdl, sizeof (struct mcxnex_sw_cq_s));
		status = IBT_INSUFF_RESOURCE;
		goto cqresize_fail;
	}
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*mr))

	(void) ddi_dma_sync(mr->mr_bindinfo.bi_dmahdl, 0,
	    new_cqinfo.qa_size, DDI_DMA_SYNC_FORDEV);

	/*
	 * Now we grab the CQ lock.  Since we will be updating the actual
	 * CQ location and the producer/consumer indexes, we should hold
	 * the lock.
	 *
	 * We do a ARBEL_NOSLEEP here (and below), though, because we are
	 * holding the "cq_lock" and if we got raised to interrupt level
	 * by priority inversion, we would not want to block in this routine
	 * waiting for success.
	 */
	mutex_enter(&cq->cq_lock);

	/*
	 * Fill in the CQC entry.  For the resize operation this is the
	 * final step before attempting the resize operation on the CQC entry.
	 * We use all of the information collected/calculated above to fill
	 * in the requisite portions of the CQC.
	 */
	bzero(&cqc_entry, sizeof (mcxnex_hw_cqc_t));
	cqc_entry.log_cq_sz	= log_cq_size;
	cqc_entry.pg_offs	= new_cqinfo.qa_pgoffs >> 5;
	cqc_entry.log2_pgsz	= mr->mr_log2_pgsz;
	cqc_entry.mtt_base_addh = (uint32_t)((mr->mr_mttaddr >> 32) & 0xFF);
	cqc_entry.mtt_base_addl = mr->mr_mttaddr >> 3;

	/*
	 * Write the CQC entry to hardware.  Lastly, we pass ownership of
	 * the entry to the hardware (using the Mcxnex RESIZE_CQ firmware
	 * command).  Note: In general, this operation shouldn't fail.  But
	 * if it does, we have to undo everything we've done above before
	 * returning error.  Also note that the status returned may indicate
	 * the code to return to the IBTF.
	 */
	status = mcxnex_resize_cq_cmd_post(state, &cqc_entry, cq->cq_cqnum,
	    &new_prod_indx, DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		/* Resize attempt has failed, drop CQ lock and cleanup */
		mutex_exit(&cq->cq_lock);
		if (mcxnex_mr_deregister(state, &mr, MCXNEX_MR_DEREG_ALL,
		    sleepflag) != DDI_SUCCESS) {
			MCXNEX_WARNING(state, "failed to deregister CQ memory");
		}
		kmem_free(resize_hdl, sizeof (struct mcxnex_sw_cq_s));
		mcxnex_queue_free(&new_cqinfo);
		if (status == MCXNEX_CMD_BAD_SIZE) {
			return (IBT_CQ_SZ_INSUFFICIENT);
		} else {
			cmn_err(CE_CONT, "Mcxnex: RESIZE_CQ command failed: "
			    "%08x\n", status);
			if (status == MCXNEX_CMD_INVALID_STATUS) {
				mcxnex_fm_ereport(state, HCA_SYS_ERR,
				    HCA_ERR_SRV_LOST);
			}
			return (DDI_FAILURE);
		}
	}

	/*
	 * For Mcxnex, we've alloc'd another handle structure and save off the
	 * important things in it. Then, in polling we check to see if there's
	 * a "resizing handle" and if so we look for the "special CQE", opcode
	 * 0x16, that indicates the transition to the new buffer.
	 *
	 * At that point, we'll adjust everything - including dereg and
	 * freeing of the original buffer, updating all the necessary fields
	 * in the cq_hdl, and setting up for the next cqe polling
	 */

	resize_hdl->cq_buf 	= buf;
	resize_hdl->cq_bufsz	= (1 << log_cq_size);
	resize_hdl->cq_mrhdl	= mr;
	resize_hdl->cq_log_cqsz = log_cq_size;

	bcopy(&new_cqinfo, &(resize_hdl->cq_cqinfo),
	    sizeof (struct mcxnex_qalloc_info_s));

	/* sync the new buffer for use by the device */
	(void) ddi_dma_sync(mr->mr_bindinfo.bi_dmahdl, 0,
	    new_cqinfo.qa_size, DDI_DMA_SYNC_FORDEV);

	/* now, save the address in the cq_handle */
	cq->cq_resize_hdl = resize_hdl;

	/*
	 * Drop the CQ lock now.
	 */

	mutex_exit(&cq->cq_lock);
	/*
	 * Fill in the return arguments (if necessary).  This includes the
	 * real new completion queue size.
	 */
	if (actual_size != NULL) {
		*actual_size = (1 << log_cq_size) - 1;
	}

	return (DDI_SUCCESS);

cqresize_fail:
	return (status);
}


/*
 * mcxnex_cq_modify()
 *    Context: Can be called base context.
 *
 * XXX - still need to implement use of the 'hid' argument.
 */
/* ARGSUSED */
int
mcxnex_cq_modify(mcxnex_state_t *state, mcxnex_cqhdl_t cq,
    uint_t count, uint_t usec, ibt_cq_handler_id_t hid, uint_t sleepflag)
{
	int	status;
	mcxnex_hw_cqc_t		cqc_entry;

	mutex_enter(&cq->cq_lock);
	if (count != cq->cq_intmod_count ||
	    usec != cq->cq_intmod_usec) {
		bzero(&cqc_entry, sizeof (mcxnex_hw_cqc_t));
		cqc_entry.cq_max_cnt = count;
		cqc_entry.cq_period = usec;
		status = mcxnex_modify_cq_cmd_post(state, &cqc_entry,
		    cq->cq_cqnum, MODIFY_MODERATION_CQ, sleepflag);
		if (status != MCXNEX_CMD_SUCCESS) {
			mutex_exit(&cq->cq_lock);
			cmn_err(CE_CONT, "Mcxnex: MODIFY_CQ command failed: "
			    "%08x\n", status);
			if (status == MCXNEX_CMD_INVALID_STATUS) {
				mcxnex_fm_ereport(state, HCA_SYS_ERR,
				    HCA_ERR_SRV_LOST);
			}
			return (DDI_FAILURE);
		}
		cq->cq_intmod_count = count;
		cq->cq_intmod_usec = usec;
	}
	mutex_exit(&cq->cq_lock);
	return (DDI_SUCCESS);
}

/*
 * mcxnex_cq_notify()
 *    Context: Can be called from interrupt or base context.
 */
int
mcxnex_cq_notify(mcxnex_state_t *state, mcxnex_cqhdl_t cq,
    ibt_cq_notify_flags_t flags)
{
	uint_t	cmd;
	ibt_status_t status;

	/* Validate IBT flags and call doorbell routine. */
	if (flags == IBT_NEXT_COMPLETION) {
		cmd = MCXNEX_CQDB_NOTIFY_CQ;
	} else if (flags == IBT_NEXT_SOLICITED) {
		cmd = MCXNEX_CQDB_NOTIFY_CQ_SOLICIT;
	} else {
		return (IBT_CQ_NOTIFY_TYPE_INVALID);
	}

	status = mcxnex_cq_arm_doorbell(state, cq, cmd);
	return (status);
}


/*
 * mcxnex_cq_poll()
 *    Context: Can be called from interrupt or base context.
 */
int
mcxnex_cq_poll(mcxnex_state_t *state, mcxnex_cqhdl_t cq, ibt_wc_t *wc_p,
    uint_t num_wc, uint_t *num_polled)
{
	mcxnex_hw_cqe_t	*cqe;
	uint_t		opcode;
	uint32_t	cons_indx, wrap_around_mask;
	uint32_t	polled_cnt, spec_op = 0;
	int		status;

	/*
	 * Check for user-mappable CQ memory.  Note:  We do not allow kernel
	 * clients to poll CQ memory that is accessible directly by the user.
	 * If the CQ memory is user accessible, then return an error.
	 */
	if (cq->cq_is_umap) {
		return (IBT_CQ_HDL_INVALID);
	}

	mutex_enter(&cq->cq_lock);

	/* Get the consumer index */
	cons_indx = cq->cq_consindx;

	/*
	 * Calculate the wrap around mask.  Note: This operation only works
	 * because all Mcxnex completion queues have power-of-2 sizes
	 */
	wrap_around_mask = (cq->cq_bufsz - 1);

	/* Calculate the pointer to the first CQ entry */
	cqe = &cq->cq_buf[cons_indx & wrap_around_mask];

	/* Sync the current CQE to read */
	mcxnex_cqe_sync(cq, cqe, DDI_DMA_SYNC_FORCPU);

	/*
	 * Keep pulling entries from the CQ until we find an entry owned by
	 * the hardware.  As long as there the CQE's owned by SW, process
	 * each entry by calling mcxnex_cq_cqe_consume() and updating the CQ
	 * consumer index.  Note:  We only update the consumer index if
	 * mcxnex_cq_cqe_consume() returns MCXNEX_CQ_SYNC_AND_DB.  Otherwise,
	 * it indicates that we are going to "recycle" the CQE (probably
	 * because it is a error CQE and corresponds to more than one
	 * completion).
	 */
	polled_cnt = 0;
	while (MCXNEX_CQE_OWNER_IS_SW(cq, cqe, cons_indx)) {
		if (cq->cq_resize_hdl != 0) {	/* in midst of resize */
			/* peek at the opcode */
			opcode = MCXNEX_CQE_OPCODE_GET(cq, cqe);
			if (opcode == MCXNEX_CQE_RCV_RESIZE_CODE) {
				mcxnex_cq_resize_helper(state, cq);

				/* Increment the consumer index */
				cons_indx = (cons_indx + 1);
				spec_op = 1; /* plus one for the limiting CQE */

				wrap_around_mask = (cq->cq_bufsz - 1);

				/* Update the pointer to the next CQ entry */
				cqe = &cq->cq_buf[cons_indx & wrap_around_mask];

				/* Sync the next CQE to read */
				mcxnex_cqe_sync(cq, cqe, DDI_DMA_SYNC_FORCPU);

				continue;
			}
		}	/* in resizing CQ */

		/*
		 * either resizing and not the special opcode, or
		 * not resizing at all
		 */
		mcxnex_cq_cqe_consume(state, cq, cqe, &wc_p[polled_cnt++]);

		/* Sync the current CQE for device */
		mcxnex_cqe_sync(cq, cqe, DDI_DMA_SYNC_FORDEV);

		/* Increment the consumer index */
		cons_indx = (cons_indx + 1);

		/* Update the pointer to the next CQ entry */
		cqe = &cq->cq_buf[cons_indx & wrap_around_mask];

		/* Sync the next CQE to read */
		mcxnex_cqe_sync(cq, cqe, DDI_DMA_SYNC_FORCPU);

		/*
		 * If we have run out of space to store work completions,
		 * then stop and return the ones we have pulled of the CQ.
		 */
		if (polled_cnt >= num_wc) {
			break;
		}
	}

	/*
	 * Now we only ring the doorbell (to update the consumer index) if
	 * we've actually consumed a CQ entry.  If we have, for example,
	 * pulled from a CQE that we are still in the process of "recycling"
	 * for error purposes, then we would not update the consumer index.
	 */
	if ((polled_cnt != 0) && (cq->cq_consindx != cons_indx)) {
		/*
		 * Update the consumer index in both the CQ handle and the
		 * doorbell record.
		 */
		cq->cq_consindx = cons_indx;
		mcxnex_cq_update_ci_doorbell(cq);

	} else if (polled_cnt == 0) {
		if (spec_op != 0) {
			/* if we got the special opcode, update the consindx */
			cq->cq_consindx = cons_indx;
			mcxnex_cq_update_ci_doorbell(cq);
		}
	}

	mutex_exit(&cq->cq_lock);

	/* Set "num_polled" (if necessary) */
	if (num_polled != NULL) {
		*num_polled = polled_cnt;
	}

	/* Set CQ_EMPTY condition if needed, otherwise return success */
	if (polled_cnt == 0) {
		status = IBT_CQ_EMPTY;
	} else {
		status = DDI_SUCCESS;
	}

	/*
	 * Check if the system is currently panicking.  If it is, then call
	 * the Mcxnex interrupt service routine.  This step is necessary here
	 * because we might be in a polled I/O mode and without the call to
	 * mcxnex_isr() - and its subsequent calls to poll and rearm each
	 * event queue - we might overflow our EQs and render the system
	 * unable to sync/dump.
	 */
	if (ddi_in_panic() != 0) {
		(void) mcxnex_isr((caddr_t)state, (caddr_t)NULL);
	}
	return (status);
}

/*
 *	cmd_sn must be initialized to 1 to enable proper reenabling
 *	by mcxnex_arm_cq_dbr_update().
 */
static void
mcxnex_arm_cq_dbr_init(mcxnex_dbr_t *cq_arm_dbr)
{
	uint32_t *target;

	target = (uint32_t *)cq_arm_dbr + 1;
	*target = htonl(1 << MCXNEX_CQDB_CMDSN_SHIFT);
}


/*
 *	User cmd_sn needs help from this kernel function to know
 *	when it should be incremented (modulo 4).  We do an atomic
 *	update of the arm_cq dbr to communicate this fact.  We retry
 *	in the case that user library is racing with us.  We zero
 *	out the cmd field so that the user library can use the cmd
 *	field to track the last command it issued (solicited verses any).
 */
static void
mcxnex_arm_cq_dbr_update(mcxnex_dbr_t *cq_arm_dbr)
{
	uint32_t tmp, cmp, new;
	uint32_t old_cmd_sn, new_cmd_sn;
	uint32_t *target;
	int retries = 0;

	target = (uint32_t *)cq_arm_dbr + 1;
retry:
	cmp = *target;
	tmp = htonl(cmp);
	old_cmd_sn = tmp & (0x3 << MCXNEX_CQDB_CMDSN_SHIFT);
	new_cmd_sn = (old_cmd_sn + (0x1 << MCXNEX_CQDB_CMDSN_SHIFT)) &
	    (0x3 << MCXNEX_CQDB_CMDSN_SHIFT);
	new = htonl((tmp & ~(0x37 << MCXNEX_CQDB_CMD_SHIFT)) | new_cmd_sn);
	tmp = atomic_cas_32(target, cmp, new);
	if (tmp != cmp) {	/* cas failed, so need to retry */
		drv_usecwait(retries & 0xff);   /* avoid race */
		if (++retries > 100000) {
			cmn_err(CE_CONT, "cas failed in mcxnex\n");
			retries = 0;
		}
		goto retry;
	}
}


/*
 * mcxnex_cq_handler()
 *    Context: Only called from interrupt context
 */
int
mcxnex_cq_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	mcxnex_cqhdl_t		cq;
	uint_t			cqnum;
	uint_t			eqe_evttype;

	eqe_evttype = MCXNEX_EQE_EVTTYPE_GET(eq, eqe);

	ASSERT(eqe_evttype == MCXNEX_EVT_COMPLETION ||
	    eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW);

	if (eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW) {
		mcxnex_eq_overflow_handler(state, eq, eqe);
		return (DDI_FAILURE);
	}

	/* Get the CQ handle from CQ number in event descriptor */
	cqnum = MCXNEX_EQE_CQNUM_GET(eq, eqe);
	cq = mcxnex_cqhdl_from_cqnum(state, cqnum);

	/*
	 * If the CQ handle is NULL, this is probably an indication
	 * that the CQ has been freed already.  In which case, we
	 * should not deliver this event.
	 *
	 * We also check that the CQ number in the handle is the
	 * same as the CQ number in the event queue entry.  This
	 * extra check allows us to handle the case where a CQ was
	 * freed and then allocated again in the time it took to
	 * handle the event queue processing.  By constantly incrementing
	 * the non-constrained portion of the CQ number every time
	 * a new CQ is allocated, we mitigate (somewhat) the chance
	 * that a stale event could be passed to the client's CQ
	 * handler.
	 *
	 * Lastly, we check if "hs_ibtfpriv" is NULL.  If it is then it
	 * means that we've have either received this event before we
	 * finished attaching to the IBTF or we've received it while we
	 * are in the process of detaching.
	 */
	if ((cq != NULL) && (cq->cq_cqnum == cqnum) &&
	    (state->hs_ibtfpriv != NULL)) {
		mcxnex_arm_cq_dbr_update(cq->cq_arm_ci_vdbr);
		MCXNEX_DO_IBTF_CQ_CALLB(state, cq);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_cq_err_handler()
 *    Context: Only called from interrupt context
 */
int
mcxnex_cq_err_handler(mcxnex_state_t *state, mcxnex_eqhdl_t eq,
    mcxnex_hw_eqe_t *eqe)
{
	mcxnex_cqhdl_t		cq;
	uint_t			cqnum;
	ibc_async_event_t	event;
	ibt_async_code_t	type;
	uint_t			eqe_evttype;

	eqe_evttype = MCXNEX_EQE_EVTTYPE_GET(eq, eqe);


	ASSERT(eqe_evttype == MCXNEX_EVT_CQ_ERRORS ||
	    eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW);

	if (eqe_evttype == MCXNEX_EVT_EQ_OVERFLOW) {
		mcxnex_eq_overflow_handler(state, eq, eqe);
		return (DDI_FAILURE);
	}

	MCXNEX_FMANOTE(state, MCXNEX_FMA_OVERRUN);
	/* Get the CQ handle from CQ number in event descriptor */
	cqnum = MCXNEX_EQE_CQNUM_GET(eq, eqe);
	cq = mcxnex_cqhdl_from_cqnum(state, cqnum);

	/*
	 * If the CQ handle is NULL, this is probably an indication
	 * that the CQ has been freed already.  In which case, we
	 * should not deliver this event.
	 *
	 * We also check that the CQ number in the handle is the
	 * same as the CQ number in the event queue entry.  This
	 * extra check allows us to handle the case where a CQ was
	 * freed and then allocated again in the time it took to
	 * handle the event queue processing.  By constantly incrementing
	 * the non-constrained portion of the CQ number every time
	 * a new CQ is allocated, we mitigate (somewhat) the chance
	 * that a stale event could be passed to the client's CQ
	 * handler.
	 *
	 * And then we check if "hs_ibtfpriv" is NULL.  If it is then it
	 * means that we've have either received this event before we
	 * finished attaching to the IBTF or we've received it while we
	 * are in the process of detaching.
	 */
	if ((cq != NULL) && (cq->cq_cqnum == cqnum) &&
	    (state->hs_ibtfpriv != NULL)) {
		event.ev_cq_hdl = (ibt_cq_hdl_t)cq->cq_hdlrarg;
		type		= IBT_ERROR_CQ;
		MCXNEX_DO_IBTF_ASYNC_CALLB(state, type, &event);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_cq_refcnt_inc()
 *    Context: Can be called from interrupt or base context.
 */
int
mcxnex_cq_refcnt_inc(mcxnex_cqhdl_t cq, uint_t is_special)
{
	/*
	 * Increment the completion queue's reference count.  Note: In order
	 * to ensure compliance with IBA C11-15, we must ensure that a given
	 * CQ is not used for both special (SMI/GSI) QP and non-special QP.
	 * This is accomplished here by keeping track of how the referenced
	 * CQ is being used.
	 */
	mutex_enter(&cq->cq_lock);
	if (cq->cq_refcnt == 0) {
		cq->cq_is_special = is_special;
	} else {
		if (cq->cq_is_special != is_special) {
			mutex_exit(&cq->cq_lock);
			return (DDI_FAILURE);
		}
	}
	cq->cq_refcnt++;
	mutex_exit(&cq->cq_lock);
	return (DDI_SUCCESS);
}


/*
 * mcxnex_cq_refcnt_dec()
 *    Context: Can be called from interrupt or base context.
 */
void
mcxnex_cq_refcnt_dec(mcxnex_cqhdl_t cq)
{
	/* Decrement the completion queue's reference count */
	mutex_enter(&cq->cq_lock);
	cq->cq_refcnt--;
	mutex_exit(&cq->cq_lock);
}


/*
 * mcxnex_cq_arm_doorbell()
 *    Context: Can be called from interrupt or base context.
 */
static int
mcxnex_cq_arm_doorbell(mcxnex_state_t *state, mcxnex_cqhdl_t cq, uint_t cq_cmd)
{
	uint32_t	cq_num;
	uint32_t	*target;
	uint32_t	old_cmd, cmp, new, tmp, cmd_sn;
	ddi_acc_handle_t uarhdl = mcxnex_get_uarhdl(state);

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test_num);

	cq_num = cq->cq_cqnum;
	target = (uint32_t *)cq->cq_arm_ci_vdbr + 1;

	/* the FMA retry loop starts for Mcxnex doorbell register. */
	mcxnex_pio_start(state, uarhdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test_num);
retry:
	cmp = *target;
	tmp = htonl(cmp);
	old_cmd = tmp & (0x7 << MCXNEX_CQDB_CMD_SHIFT);
	cmd_sn = tmp & (0x3 << MCXNEX_CQDB_CMDSN_SHIFT);
	if (cq_cmd == MCXNEX_CQDB_NOTIFY_CQ) {
		if (old_cmd != MCXNEX_CQDB_NOTIFY_CQ) {
			cmd_sn |= (MCXNEX_CQDB_NOTIFY_CQ <<
			    MCXNEX_CQDB_CMD_SHIFT);
			new = htonl(cmd_sn | (cq->cq_consindx & 0xFFFFFF));
			tmp = atomic_cas_32(target, cmp, new);
			if (tmp != cmp)
				goto retry;
			MCXNEX_UAR_DOORBELL(state, uarhdl, (uint64_t *)(void *)
			    &state->hs_uar->cq, (((uint64_t)cmd_sn | cq_num) <<
			    32) | (cq->cq_consindx & 0xFFFFFF));
		} /* else it's already armed */
	} else {
		ASSERT(cq_cmd == MCXNEX_CQDB_NOTIFY_CQ_SOLICIT);
		if (old_cmd != MCXNEX_CQDB_NOTIFY_CQ &&
		    old_cmd != MCXNEX_CQDB_NOTIFY_CQ_SOLICIT) {
			cmd_sn |= (MCXNEX_CQDB_NOTIFY_CQ_SOLICIT <<
			    MCXNEX_CQDB_CMD_SHIFT);
			new = htonl(cmd_sn | (cq->cq_consindx & 0xFFFFFF));
			tmp = atomic_cas_32(target, cmp, new);
			if (tmp != cmp)
				goto retry;
			MCXNEX_UAR_DOORBELL(state, uarhdl, (uint64_t *)(void *)
			    &state->hs_uar->cq, (((uint64_t)cmd_sn | cq_num) <<
			    32) | (cq->cq_consindx & 0xFFFFFF));
		} /* else it's already armed */
	}

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, uarhdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test_num);

	return (IBT_SUCCESS);

pio_error:
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_SRV_LOST);
	return (DDI_FAILURE);
}


/*
 * mcxnex_cqhdl_from_cqnum()
 *    Context: Can be called from interrupt or base context.
 *
 *    This routine is important because changing the unconstrained
 *    portion of the CQ number is critical to the detection of a
 *    potential race condition in the CQ handler code (i.e. the case
 *    where a CQ is freed and alloc'd again before an event for the
 *    "old" CQ can be handled).
 *
 *    While this is not a perfect solution (not sure that one exists)
 *    it does help to mitigate the chance that this race condition will
 *    cause us to deliver a "stale" event to the new CQ owner.  Note:
 *    this solution does not scale well because the number of constrained
 *    bits increases (and, hence, the number of unconstrained bits
 *    decreases) as the number of supported CQs grows.  For small and
 *    intermediate values, it should hopefully provide sufficient
 *    protection.
 */
mcxnex_cqhdl_t
mcxnex_cqhdl_from_cqnum(mcxnex_state_t *state, uint_t cqnum)
{
	uint_t	cqindx, cqmask;

	/* Calculate the CQ table index from the cqnum */
	cqmask = (1 << state->hs_cfg_profile->cp_log_num_cq) - 1;
	cqindx = cqnum & cqmask;
	return (state->hs_cqhdl[cqindx]);
}

/*
 * mcxnex_cq_cqe_consume()
 *    Context: Can be called from interrupt or base context.
 */
static void
mcxnex_cq_cqe_consume(mcxnex_state_t *state, mcxnex_cqhdl_t cq,
    mcxnex_hw_cqe_t *cqe, ibt_wc_t *wc)
{
	uint_t		opcode, qpnum, qp1_indx;
	ibt_wc_flags_t	flags;
	ibt_wrc_opcode_t type;

	/*
	 * Determine if this is an "error" CQE by examining "opcode".  If it
	 * is an error CQE, then call mcxnex_cq_errcqe_consume() and return
	 * whatever status it returns.  Otherwise, this is a successful
	 * completion.
	 */
	opcode = MCXNEX_CQE_OPCODE_GET(cq, cqe);
	if ((opcode == MCXNEX_CQE_SEND_ERR_OPCODE) ||
	    (opcode == MCXNEX_CQE_RECV_ERR_OPCODE)) {
		mcxnex_cq_errcqe_consume(state, cq, cqe, wc);
		return;
	}

	/*
	 * Fetch the Work Request ID using the information in the CQE.
	 * See mcxnex_wr.c for more details.
	 */
	wc->wc_id = mcxnex_wrid_get_entry(cq, cqe);

	/*
	 * Parse the CQE opcode to determine completion type.  This will set
	 * not only the type of the completion, but also any flags that might
	 * be associated with it (e.g. whether immediate data is present).
	 */
	flags = IBT_WC_NO_FLAGS;
	if (MCXNEX_CQE_SENDRECV_GET(cq, cqe) != MCXNEX_COMPLETION_RECV) {

		/* Send CQE */
		switch (opcode) {
		case MCXNEX_CQE_SND_RDMAWR_IMM:
			flags |= IBT_WC_IMMED_DATA_PRESENT;
			/* FALLTHROUGH */
		case MCXNEX_CQE_SND_RDMAWR:
			type = IBT_WRC_RDMAW;
			break;

		case MCXNEX_CQE_SND_SEND_IMM:
			flags |= IBT_WC_IMMED_DATA_PRESENT;
			/* FALLTHROUGH */
		case MCXNEX_CQE_SND_SEND:
			type = IBT_WRC_SEND;
			break;

		case MCXNEX_CQE_SND_LSO:
			type = IBT_WRC_SEND_LSO;
			break;

		case MCXNEX_CQE_SND_RDMARD:
			type = IBT_WRC_RDMAR;
			break;

		case MCXNEX_CQE_SND_ATOMIC_CS:
			type = IBT_WRC_CSWAP;
			break;

		case MCXNEX_CQE_SND_ATOMIC_FA:
			type = IBT_WRC_FADD;
			break;

		case MCXNEX_CQE_SND_BIND_MW:
			type = IBT_WRC_BIND;
			break;

		default:
			MCXNEX_WARNING(state, "unknown send CQE type");
			wc->wc_status = IBT_WC_LOCAL_QP_OP_ERR;
			return;
		}
	} else {

		/* Receive CQE */
		switch (opcode & 0x1F) {
		/* for sendonly w/imm or sendlast w/imm */
		case MCXNEX_CQE_RCV_SEND_IMM:
			/*
			 * Note:  According to the PRM, all QP1 recv
			 * completions look like the result of a Send with
			 * Immediate.  They are not, however, (MADs are Send
			 * Only) so we need to check the QP number and set
			 * the flag only if it is non-QP1.
			 */
			qpnum	 = MCXNEX_CQE_QPNUM_GET(cq, cqe);
			qp1_indx = state->hs_spec_qp1->hr_indx;
			if ((qpnum < qp1_indx) || (qpnum > qp1_indx + 1)) {
				flags |= IBT_WC_IMMED_DATA_PRESENT;
			}
			/* FALLTHROUGH */
		/* for sendonly or sendlast */
		case MCXNEX_CQE_RCV_SEND:
			type = IBT_WRC_RECV;
			wc->wc_vlanid = 0;
			if (MCXNEX_CQE_IS_VLAN(cq, cqe)) {
				wc->wc_vlanid = MCXNEX_CQE_VLANID(cq, cqe);
			}

			if (MCXNEX_CQE_IS_IPOK(cq, cqe)) {
				wc->wc_cksum = MCXNEX_CQE_CKSUM(cq, cqe);
				flags |= IBT_WC_CKSUM_OK;
				if (MCXNEX_CQE_IS_TCPUDP(cq, cqe))
					flags |= IBT_WC_CKSUM_TCP_UDP_OK;
				wc->wc_detail = IBT_WC_DETAIL_ALL_FLAGS_MASK &
				    MCXNEX_CQE_IPOIB_STATUS(cq, cqe);
			}
			break;
		/* for RDMAwrite only or RDMAwrite last w/imm */
		case MCXNEX_CQE_RCV_RDMAWR_IMM:
			flags |= IBT_WC_IMMED_DATA_PRESENT;
			type = IBT_WRC_RECV_RDMAWI;
			break;

		default:
		/* still don't support send/invalidate, need to add later */

			MCXNEX_WARNING(state, "unknown recv CQE type");
			wc->wc_status = IBT_WC_LOCAL_QP_OP_ERR;
			return;
		}
	}
	wc->wc_type = type;

	/*
	 * Check for GRH, update the flags, then fill in "wc_flags" field
	 * in the work completion
	 */
	if (MCXNEX_CQE_GRH_GET(cq, cqe) != 0) {
		flags |= IBT_WC_GRH_PRESENT;
	}
	wc->wc_flags = flags;

	/* If we got here, completion status must be success */
	wc->wc_status = IBT_WC_SUCCESS;

	/*
	 * Parse the remaining contents of the CQE into the work completion.
	 * This means filling in SL, QP number, SLID, immediate data, etc.
	 * Note:  Not all of these fields are valid in a given completion.
	 * Many of them depend on the actual type of completion.  So we fill
	 * in all of the fields and leave it up to the IBTF and consumer to
	 * sort out which are valid based on their context.
	 */
	wc->wc_sl	  = MCXNEX_CQE_SL_GET(cq, cqe);
	wc->wc_immed_data = MCXNEX_CQE_IMM_ETH_PKEY_CRED_GET(cq, cqe);
	wc->wc_qpn	  = MCXNEX_CQE_DQPN_GET(cq, cqe);
	wc->wc_slid	  = MCXNEX_CQE_DLID_GET(cq, cqe);
	wc->wc_ethertype  = (wc->wc_immed_data & 0xFFFF);
	wc->wc_pkey_ix	  = (wc->wc_immed_data &
	    ((1 << state->hs_queryport.log_max_pkey) - 1));
	/*
	 * Depending on whether the completion was a receive or a send
	 * completion, fill in "bytes transferred" as appropriate.  Also,
	 * if necessary, fill in the "path bits" field.
	 */
	if (MCXNEX_CQE_SENDRECV_GET(cq, cqe) == MCXNEX_COMPLETION_RECV) {
		wc->wc_path_bits = MCXNEX_CQE_PATHBITS_GET(cq, cqe);
		wc->wc_bytes_xfer = MCXNEX_CQE_BYTECNT_GET(cq, cqe);

	} else if ((wc->wc_type == IBT_WRC_RDMAR) ||
	    (wc->wc_type == IBT_WRC_CSWAP) || (wc->wc_type == IBT_WRC_FADD)) {
		wc->wc_bytes_xfer = MCXNEX_CQE_BYTECNT_GET(cq, cqe);
	}
}

/*
 * mcxnex_cq_errcqe_consume()
 *    Context: Can be called from interrupt or base context.
 */
static void
mcxnex_cq_errcqe_consume(mcxnex_state_t *state, mcxnex_cqhdl_t cq,
    mcxnex_hw_cqe_t *cqe, ibt_wc_t *wc)
{
	uint32_t		imm_eth_pkey_cred;
	uint_t			status;
	ibt_wc_status_t		ibt_status;

	/*
	 * Fetch the Work Request ID using the information in the CQE.
	 * See mcxnex_wr.c for more details.
	 */
	wc->wc_id = mcxnex_wrid_get_entry(cq, cqe);

	/*
	 * Parse the CQE opcode to determine completion type.  We know that
	 * the CQE is an error completion, so we extract only the completion
	 * status/syndrome here.
	 */
	imm_eth_pkey_cred = MCXNEX_CQE_ERROR_SYNDROME_GET(cq, cqe);
	status = imm_eth_pkey_cred;
	switch (status) {
	case MCXNEX_CQE_LOC_LEN_ERR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_LOCLEN);
		ibt_status = IBT_WC_LOCAL_LEN_ERR;
		break;

	case MCXNEX_CQE_LOC_OP_ERR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_LOCQPOP);
		ibt_status = IBT_WC_LOCAL_QP_OP_ERR;
		break;

	case MCXNEX_CQE_LOC_PROT_ERR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_LOCPROT);
		ibt_status = IBT_WC_LOCAL_PROTECT_ERR;
		if (mcxnex_should_panic) {
			cmn_err(CE_PANIC, "Local Protection Error");
		}
		break;

	case MCXNEX_CQE_WR_FLUSHED_ERR:
		ibt_status = IBT_WC_WR_FLUSHED_ERR;
		break;

	case MCXNEX_CQE_MW_BIND_ERR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_MWBIND);
		ibt_status = IBT_WC_MEM_WIN_BIND_ERR;
		break;

	case MCXNEX_CQE_BAD_RESPONSE_ERR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_RESP);
		ibt_status = IBT_WC_BAD_RESPONSE_ERR;
		break;

	case MCXNEX_CQE_LOCAL_ACCESS_ERR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_LOCACC);
		ibt_status = IBT_WC_LOCAL_ACCESS_ERR;
		break;

	case MCXNEX_CQE_REM_INV_REQ_ERR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_REMREQ);
		ibt_status = IBT_WC_REMOTE_INVALID_REQ_ERR;
		break;

	case MCXNEX_CQE_REM_ACC_ERR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_REMACC);
		ibt_status = IBT_WC_REMOTE_ACCESS_ERR;
		break;

	case MCXNEX_CQE_REM_OP_ERR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_REMOP);
		ibt_status = IBT_WC_REMOTE_OP_ERR;
		break;

	case MCXNEX_CQE_TRANS_TO_ERR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_XPORTCNT);
		ibt_status = IBT_WC_TRANS_TIMEOUT_ERR;
		break;

	case MCXNEX_CQE_RNRNAK_TO_ERR:
		MCXNEX_FMANOTE(state, MCXNEX_FMA_RNRCNT);
		ibt_status = IBT_WC_RNR_NAK_TIMEOUT_ERR;
		break;

	/*
	 * The following error codes are not supported in the Mcxnex driver
	 * as they relate only to Reliable Datagram completion statuses:
	 *    case MCXNEX_CQE_LOCAL_RDD_VIO_ERR:
	 *    case MCXNEX_CQE_REM_INV_RD_REQ_ERR:
	 *    case MCXNEX_CQE_EEC_REM_ABORTED_ERR:
	 *    case MCXNEX_CQE_INV_EEC_NUM_ERR:
	 *    case MCXNEX_CQE_INV_EEC_STATE_ERR:
	 *    case MCXNEX_CQE_LOC_EEC_ERR:
	 */

	default:
		MCXNEX_WARNING(state, "unknown error CQE status");
		MCXNEX_FMANOTE(state, MCXNEX_FMA_UNKN);
		ibt_status = IBT_WC_LOCAL_QP_OP_ERR;
		break;
	}

	wc->wc_status = ibt_status;
}


/*
 * mcxnex_cqe_sync()
 *    Context: Can be called from interrupt or base context.
 */
static void
mcxnex_cqe_sync(mcxnex_cqhdl_t cq, mcxnex_hw_cqe_t *cqe, uint_t flag)
{
	ddi_dma_handle_t	dmahdl;
	off_t			offset;
	int			status;

	/* Get the DMA handle from CQ context */
	dmahdl = cq->cq_mrhdl->mr_bindinfo.bi_dmahdl;

	/* Calculate offset of next CQE */
	offset = (off_t)((uintptr_t)cqe - (uintptr_t)&cq->cq_buf[0]);
	status = ddi_dma_sync(dmahdl, offset, sizeof (mcxnex_hw_cqe_t), flag);
	if (status != DDI_SUCCESS) {
		return;
	}
}


/*
 * mcxnex_cq_resize_helper()
 *    Context: Can be called only from user or kernel context.
 */
void
mcxnex_cq_resize_helper(mcxnex_state_t *state, mcxnex_cqhdl_t cq)
{
	mcxnex_cqhdl_t 		resize_hdl;
	int			status;

	/*
	 * we're here because we found the special cqe opcode, so we have
	 * to update the cq_handle, release the old resources, clear the
	 * flag in the cq_hdl, and release the resize_hdl.  When we return
	 * above, it will take care of the rest
	 */
	ASSERT(MUTEX_HELD(&cq->cq_lock));

	resize_hdl = cq->cq_resize_hdl;

	/*
	 * Deregister the memory for the old Completion Queue.  Note: We
	 * really can't return error here because we have no good way to
	 * cleanup.  Plus, the deregistration really shouldn't ever happen.
	 * So, if it does, it is an indication that something has gone
	 * seriously wrong.  So we print a warning message and return error
	 * (knowing, of course, that the "old" CQ memory will be leaked)
	 */
	status = mcxnex_mr_deregister(state, &cq->cq_mrhdl, MCXNEX_MR_DEREG_ALL,
	    DDI_SLEEP);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "failed to deregister old CQ memory");
	}

	/* Next, free the memory from the old CQ buffer */
	mcxnex_queue_free(&cq->cq_cqinfo);

	/* now we can update the cq_hdl with the new things saved */

	cq->cq_buf   = resize_hdl->cq_buf;
	cq->cq_mrhdl = resize_hdl->cq_mrhdl;
	cq->cq_bufsz = resize_hdl->cq_bufsz;
	cq->cq_log_cqsz = resize_hdl->cq_log_cqsz;
	cq->cq_umap_dhp = cq->cq_resize_hdl->cq_umap_dhp;
	cq->cq_resize_hdl = 0;
	bcopy(&resize_hdl->cq_cqinfo, &cq->cq_cqinfo,
	    sizeof (struct mcxnex_qalloc_info_s));

	/* finally, release the resizing handle */
	kmem_free(resize_hdl, sizeof (struct mcxnex_sw_cq_s));
}


/*
 * mcxnex_cq_entries_flush()
 * Context: Can be called from interrupt or base context.
 */
/* ARGSUSED */
void
mcxnex_cq_entries_flush(mcxnex_state_t *state, mcxnex_qphdl_t qp)
{
	mcxnex_cqhdl_t		cq;
	mcxnex_hw_cqe_t		*cqe, *next_cqe;
	mcxnex_srqhdl_t		srq;
	mcxnex_workq_hdr_t	*wq;
	uint32_t		cons_indx, tail_cons_indx, wrap_around_mask;
	uint32_t		new_indx, check_indx, qpnum;
	int			outstanding_cqes;

	qpnum = qp->qp_qpnum;
	if ((srq = qp->qp_srqhdl) != NULL)
		wq = qp->qp_srqhdl->srq_wq_wqhdr;
	else
		wq = NULL;
	cq = qp->qp_rq_cqhdl;

do_send_cq:	/* loop back to here if send_cq is not the same as recv_cq */

	cons_indx = cq->cq_consindx;
	wrap_around_mask = (cq->cq_bufsz - 1);

	/* Calculate the pointer to the first CQ entry */
	cqe = &cq->cq_buf[cons_indx & wrap_around_mask];

	/* Sync the current CQE to read */
	mcxnex_cqe_sync(cq, cqe, DDI_DMA_SYNC_FORCPU);

	/*
	 * Loop through the CQ looking for entries owned by software.  If an
	 * entry is owned by software then we increment an 'outstanding_cqes'
	 * count to know how many entries total we have on our CQ.  We use this
	 * value further down to know how many entries to loop through looking
	 * for our same QP number.
	 */
	outstanding_cqes = 0;
	tail_cons_indx = cons_indx;
	while (MCXNEX_CQE_OWNER_IS_SW(cq, cqe, tail_cons_indx)) {
		/* increment total cqes count */
		outstanding_cqes++;

		/* increment the consumer index */
		tail_cons_indx++;

		/* update the pointer to the next cq entry */
		cqe = &cq->cq_buf[tail_cons_indx & wrap_around_mask];

		/* sync the next cqe to read */
		mcxnex_cqe_sync(cq, cqe, DDI_DMA_SYNC_FORCPU);
	}

	/*
	 * Using the 'tail_cons_indx' that was just set, we now know how many
	 * total CQEs possible there are.  Set the 'check_indx' and the
	 * 'new_indx' to the last entry identified by 'tail_cons_indx'
	 */
	check_indx = new_indx = (tail_cons_indx - 1);

	while (--outstanding_cqes >= 0) {
		cqe = &cq->cq_buf[check_indx & wrap_around_mask];

		/*
		 * If the QP number is the same in the CQE as the QP, then
		 * we must "consume" it.  If it is for an SRQ wqe, then we
		 * also must free the wqe back onto the free list of the SRQ.
		 */
		if (qpnum == MCXNEX_CQE_QPNUM_GET(cq, cqe)) {
			if (srq && (MCXNEX_CQE_SENDRECV_GET(cq, cqe) ==
			    MCXNEX_COMPLETION_RECV)) {
				uint64_t *desc;
				int indx;

				/* Add wqe back to SRQ free list */
				indx = MCXNEX_CQE_WQEADDRSZ_GET(cq, cqe) &
				    wq->wq_mask;
				desc = MCXNEX_SRQ_WQE_ADDR(srq, wq->wq_tail);
				((uint16_t *)desc)[1] = htons(indx);
				wq->wq_tail = indx;
			}
		} else {	/* CQEs for other QPNs need to remain */
			if (check_indx != new_indx) {
				next_cqe =
				    &cq->cq_buf[new_indx & wrap_around_mask];
				/* Copy the CQE into the "next_cqe" pointer. */
				bcopy(cqe, next_cqe, sizeof (mcxnex_hw_cqe_t));
			}
			new_indx--;	/* move index to next CQE to fill */
		}
		check_indx--;		/* move index to next CQE to check */
	}

	/*
	 * Update consumer index to be the 'new_indx'.  This moves it past all
	 * removed entries.  Because 'new_indx' is pointing to the last
	 * previously valid SW owned entry, we add 1 to point the cons_indx to
	 * the first HW owned entry.
	 */
	cons_indx = (new_indx + 1);

	/*
	 * Now we only ring the doorbell (to update the consumer index) if
	 * we've actually consumed a CQ entry.  If we found no QP number
	 * matches above, then we would not have removed anything.  So only if
	 * something was removed do we ring the doorbell.
	 */
	if (cq->cq_consindx != cons_indx) {
		/*
		 * Update the consumer index in both the CQ handle and the
		 * doorbell record.
		 */
		cq->cq_consindx = cons_indx;

		mcxnex_cq_update_ci_doorbell(cq);

	}
	if (cq != qp->qp_sq_cqhdl) {
		cq = qp->qp_sq_cqhdl;
		goto do_send_cq;
	}
}


void
mcxnex_priv_cq_handler(mcxnex_state_t *state, mcxnex_cqhdl_t cq_hdl)
{
	mcxnex_priv_cq_cb_t  cb;
	void		    *cb_arg;

	cb = cq_hdl->cq_priv_cb;
	cb_arg = cq_hdl->cq_priv_cb_arg;

	if (cb != NULL)
		cb(state, cq_hdl, cb_arg);

	/* for IB driver, need to call ibc_cq_handler() */
}

/*ARGSUSED*/
void
mcxnex_priv_cq_set_handler(mcxnex_state_t *state, mcxnex_cqhdl_t cq_hdl,
    mcxnex_priv_cq_cb_t cq_cb, void *cq_cb_arg)
{
	cq_hdl->cq_priv_cb = cq_cb;
	cq_hdl->cq_priv_cb_arg = cq_cb_arg;
}
